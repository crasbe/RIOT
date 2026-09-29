"""Buildbot steps that check out RIOT and compute/fan out its compile-job matrix.

The matrix is built in two stages, so the slow part is spread over the
workers:

1. The coordinator decides which applications to build, and for which
   boards (`SelectBuilds`, cheap), and splits the applications into batches
   (`TriggerJobLists`).
2. Each batch is a "list-jobs" build on any free worker, which queries its
   applications for their (board, toolchain) combinations
   (`ComputeCompileJobs`) and triggers a "compile" build for each
   (`TriggerCompileJobs`). Compile builds therefore start while other
   batches are still being listed.
"""
import json
from urllib.parse import urlsplit

from buildbot.plugins import steps, util
from buildbot.process.properties import renderer
from buildbot.process.results import ALL_RESULTS, SKIPPED, SUCCESS, statusToString
from buildbot.steps.trigger import Trigger
from buildbot.util import join_list

# Where the buildbot-worker image (riotdocker/buildbot-worker/) keeps
# git-cache's repository mirrors: on its persistent /cache volume.
GIT_CACHE_DIR = "/cache/.gitcache"


def git_cache_mirror(repo_url):
    """Return the path of git-cache's bare mirror of a repository on a worker.

    Args:
        repo_url: URL of the repository.

    Returns:
        The mirror's path, e.g. "/cache/.gitcache/github.com/RIOT-OS/RIOT.git"
        for "https://github.com/RIOT-OS/RIOT", or None for local
        repositories, which git-cache doesn't mirror.
    """
    url = urlsplit(repo_url)
    if url.scheme in ("", "file") or not url.hostname:
        return None
    path = url.path.strip("/")
    if not path.endswith(".git"):
        path += ".git"
    return f"{GIT_CACHE_DIR}/{url.hostname}/{path}"


def _is_pr_build(step):
    return bool(step.getProperty("pr_number"))


def _needs_pr_head_resolve(step):
    return _is_pr_build(step) and not step.getProperty("pr_head_sha")


def _hide_if_skipped(results, step):
    return results == SKIPPED


def _plural(count, noun):
    return f"{count} {noun}" if count == 1 else f"{count} {noun}s"


def _pr_properties(step):
    """The PR properties a triggered build needs to reproduce this build's merge."""
    if not step.getProperty("pr_number"):
        return {}
    return {"pr_number": step.getProperty("pr_number"),
            "pr_head_sha": step.getProperty("pr_head_sha")}


def _json_property(step, name):
    return step.getProperty(name) or {}


def _json_extractor(name):
    """Build an extract_fn for SetPropertyFromCommand that parses the
    command's JSON output into the property `name`.

    Raises (making the step fail with an exception) on invalid JSON, rather
    than letting later steps mistake it for an empty result.
    """

    def extract(rc, stdout, stderr):
        try:
            return {name: json.loads(stdout)}
        except json.JSONDecodeError as e:
            raise ValueError(f"jobs.py printed invalid JSON ({e})") from e

    return extract


def checkout_steps(repo_url):
    """Build the steps that check out the code a build is supposed to test.

    The base is whatever the build's sourcestamp points at (a branch for
    forced builds, the coordinator's exact base SHA for triggered builds).
    If the `pr_number` property is set, the PR's head is fetched from
    `repo_url` and merged into that base with `git merge --no-ff`, the same
    way GitHub would merge it. The PR head is resolved to a SHA once, in the
    first build (stored as `pr_head_sha`); builds that already carry
    `pr_head_sha` fetch exactly that commit, so every build of one CI run
    tests the identical merge.

    For remote repositories, the checkout borrows its git objects from
    git-cache's mirror on the worker's persistent /cache volume (git
    alternates), so the checkout itself only holds the files and can live
    in RAM. The mirror is only fetched if it lacks the commit to build.

    Args:
        repo_url: URL of the RIOT repository to fetch base and PR from.

    Returns:
        A list of steps to add to a BuildFactory, in order.
    """
    mirror = git_cache_mirror(repo_url)

    @renderer
    def update_mirror_command(props):
        # "revision" is empty for the coordinator's branch builds, which
        # therefore always update the mirror.
        return [
            "sh", "-c",
            'if [ -n "$1" ] && git --git-dir="$2" cat-file -e "$1^{commit}" 2>/dev/null; then'
            ' echo "mirror already contains $1";'
            ' else exec git-cache prefetch --update "$3"; fi',
            "sh", props.getProperty("revision") or "", mirror, repo_url,
        ]

    # Buildbot renders a step's command before evaluating doStepIf, so these
    # must not fail for non-PR builds, where the PR properties are unset.
    @renderer
    def fetch_pr_command(props):
        pr_head_sha = props.getProperty("pr_head_sha")
        if pr_head_sha:
            return ["git", "fetch", repo_url, pr_head_sha]
        pr_number = int(props.getProperty("pr_number") or 0)
        return ["git", "fetch", repo_url, f"+refs/pull/{pr_number}/head"]

    @renderer
    def merge_pr_command(props):
        return ["git", "merge", "--no-ff", "--no-edit", props.getProperty("pr_head_sha") or ""]

    mirror_steps = []
    if mirror:
        mirror_steps = [steps.ShellCommand(
            name="update-mirror", command=update_mirror_command,
            env={"GIT_CACHE_DIR": GIT_CACHE_DIR}, haltOnFailure=True,
            description="Updating repository mirror",
            descriptionDone="Repository mirror up to date")]

    return mirror_steps + [
        steps.Git(name="checkout-base", repourl=repo_url, mode="full", method="fresh",
                  reference=mirror,
                  description="Checking out base",
                  descriptionDone="Checked out base"),
        steps.ShellCommand(
            name="fetch-pr", command=fetch_pr_command,
            doStepIf=_is_pr_build, hideStepIf=_hide_if_skipped, haltOnFailure=True,
            description=util.Interpolate("Fetching PR #%(prop:pr_number)s"),
            descriptionDone=util.Interpolate("Fetched PR #%(prop:pr_number)s")),
        steps.SetPropertyFromCommand(
            name="resolve-pr-head", command=["git", "rev-parse", "FETCH_HEAD"],
            property="pr_head_sha",
            doStepIf=_needs_pr_head_resolve, hideStepIf=_hide_if_skipped, haltOnFailure=True,
            description="Resolving PR head",
            descriptionDone=util.Interpolate("PR head is %(prop:pr_head_sha)s")),
        steps.ShellCommand(
            name="merge-pr", command=merge_pr_command,
            doStepIf=_is_pr_build, hideStepIf=_hide_if_skipped, haltOnFailure=True,
            description=util.Interpolate("Merging PR #%(prop:pr_number)s"),
            descriptionDone=util.Interpolate("Merged PR #%(prop:pr_number)s")),
    ]


def _jobs_py_command(select_only):
    """Build a renderer for the `riot_ci/jobs.py` command line.

    Translates the `boards`, `apps`, `full_build` and `quick_build` build
    properties (all optional) into the matching `jobs.py` flags. For PR
    builds, change detection diffs against the checked-out base
    (`got_revision`); other builds skip change detection.

    Args:
        select_only: Pass `--select-only`, i.e. only decide which
            applications and boards to build.
    """

    @renderer
    def command(props):
        cmd = ["python3", ".buildbot/riot_ci/jobs.py"]
        if select_only:
            cmd.append("--select-only")
        boards = props.getProperty("boards")
        if boards:
            cmd += ["--boards", boards]
        apps = props.getProperty("apps")
        if apps:
            cmd += ["--apps", apps]
        if props.getProperty("full_build"):
            cmd.append("--full-build")
        if props.getProperty("quick_build"):
            cmd.append("--quick-build")
        if props.getProperty("pr_number"):
            cmd += ["--upstream-commit", props.getProperty("got_revision")]
        return cmd

    return command


class SelectBuilds(steps.SetPropertyFromCommand):
    """Decides which applications to build, and for which boards.

    Runs `riot_ci/jobs.py --select-only` on the worker and stores its parsed
    JSON output (`{"apps": [...], "boards": [...] or null, "errors": [...]}`)
    as the `selection`
    build property, for `TriggerJobLists` to consume.
    """

    name = "select-builds"

    def __init__(self, **kwargs):
        kwargs.setdefault("command", _jobs_py_command(select_only=True))
        kwargs.setdefault("extract_fn", _json_extractor("selection"))
        kwargs.setdefault("haltOnFailure", True)
        kwargs.setdefault("description", "Selecting applications and boards")
        super().__init__(**kwargs)

    def getResultSummary(self):
        if self.results != SUCCESS:
            return {"step": f"{join_list(self.description)} ({statusToString(self.results)})"}
        selection = _json_property(self, "selection")
        apps = len(selection.get("apps", []))
        if not apps:
            return {"step": "Nothing to build"}
        boards = selection.get("boards")
        boards = _plural(len(boards), "board") if boards else "all supported boards"
        return {"step": f"Selected {_plural(apps, 'application')} for {boards}"}


class _CountingTrigger(Trigger):
    """A Trigger whose summary counts the triggered builds instead of listing
    the scheduler once per build ("triggered trigger-compile, trigger-compile, ...").
    """

    what = "build"

    def getSchedulersAndProperties(self):
        triggers = self.getTriggers()
        self.triggered = len(triggers)
        return triggers

    def getCurrentSummary(self):
        if not hasattr(self, "triggered"):
            return {"step": join_list(self.description)}
        summary = f"Triggered {_plural(self.triggered, self.what)}"
        # Results of the triggered builds that finished so far (only
        # collected with waitForFinish=True); Trigger's own bookkeeping.
        finished = self._result_list
        counts = [f"{finished.count(r)} {statusToString(r, finished.count(r))}"
                  for r in ALL_RESULTS if finished.count(r)]
        if counts:
            summary += f" ({', '.join(counts)})"
        return {"step": summary}

    def getResultSummary(self):
        return self.getCurrentSummary()


class TriggerJobLists(_CountingTrigger):
    """Triggers one `list-jobs` build per batch of selected applications.

    Reads the `selection` property set by `SelectBuilds` and splits its
    applications into batches of `apps_per_batch`. Each batch becomes a
    `list-jobs` build that lists and triggers the compile jobs of its
    applications, for the selected boards. The base revision is passed on
    through the sourcestamp (`updateSourceStamp`), the PR (if any) through
    properties.

    Args:
        apps_per_batch: Number of applications per `list-jobs` build.
    """

    what = "list-jobs build"

    def __init__(self, apps_per_batch, **kwargs):
        self.apps_per_batch = apps_per_batch
        kwargs.setdefault("description", "Handing out application batches")
        super().__init__(**kwargs)

    def getTriggers(self):
        selection = _json_property(self, "selection")
        apps = selection.get("apps", [])
        boards = " ".join(selection.get("boards") or [])
        batches = [apps[i:i + self.apps_per_batch]
                   for i in range(0, len(apps), self.apps_per_batch)]
        return [
            {
                "sched_name": "trigger-list-jobs",
                "props_to_set": {
                    "apps": " ".join(batch),
                    # empty: all boards each application supports
                    "boards": boards,
                    # the selection is final; don't run change detection again
                    "full_build": True,
                    **_pr_properties(self),
                },
                "unimportant": False,
            }
            for batch in batches
        ]


class ComputeCompileJobs(steps.SetPropertyFromCommand):
    """Lists the compile jobs of the applications in the `apps` property.

    Runs `riot_ci/jobs.py` on the worker and stores its parsed JSON output as the
    `compile_jobs` build property, for `TriggerCompileJobs` to consume.
    Fails the build, without triggering anything, if `jobs.py` couldn't
    query an application (e.g. a broken Makefile).
    """

    name = "compute-compile-jobs"

    def __init__(self, **kwargs):
        kwargs.setdefault("command", _jobs_py_command(select_only=False))
        kwargs.setdefault("extract_fn", _json_extractor("compile_jobs"))
        kwargs.setdefault("haltOnFailure", True)
        kwargs.setdefault("description", "Listing compile jobs")
        super().__init__(**kwargs)

    def getResultSummary(self):
        if self.results != SUCCESS:
            return {"step": f"{join_list(self.description)} ({statusToString(self.results)})"}
        jobs = len(_json_property(self, "compile_jobs").get("jobs", []))
        return {"step": f"Listed {_plural(jobs, 'compile job')}"}


class TriggerCompileJobs(_CountingTrigger):
    """Triggers one `compile` build per job in the `compile_jobs` property.

    `compile_jobs` is expected to hold the JSON produced by
    `riot_ci/jobs.py` (typically set by a preceding `ComputeCompileJobs`
    step): `{"jobs": [{"appdir": ..., "board": ..., "toolchain": ...}, ...]}`.
    Each job becomes one triggered build carrying its appdir/board/toolchain
    as build properties, plus `pr_number`/`pr_head_sha` for PR builds. The
    base revision is passed on through the sourcestamp (`updateSourceStamp`),
    so all triggered builds check out the same base as this build.
    """

    what = "compile build"

    def __init__(self, **kwargs):
        kwargs.setdefault("description", "Triggering compile builds")
        super().__init__(**kwargs)

    def getTriggers(self):
        jobs = _json_property(self, "compile_jobs").get("jobs", [])
        return [
            {
                "sched_name": "trigger-compile",
                "props_to_set": {
                    "appdir": job["appdir"],
                    "board": job["board"],
                    "toolchain": job["toolchain"],
                    **_pr_properties(self),
                },
                "unimportant": False,
            }
            for job in jobs
        ]
