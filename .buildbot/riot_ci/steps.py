"""Buildbot steps that check out RIOT and compute/fan out its compile-job matrix."""
import json
from urllib.parse import urlsplit

from buildbot.plugins import steps
from buildbot.process.properties import renderer
from buildbot.steps.trigger import Trigger

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
            env={"GIT_CACHE_DIR": GIT_CACHE_DIR}, haltOnFailure=True)]

    return mirror_steps + [
        steps.Git(name="checkout-base", repourl=repo_url, mode="full", method="fresh",
                  reference=mirror),
        steps.ShellCommand(
            name="fetch-pr", command=fetch_pr_command,
            doStepIf=_is_pr_build, haltOnFailure=True),
        steps.SetPropertyFromCommand(
            name="resolve-pr-head", command=["git", "rev-parse", "FETCH_HEAD"],
            property="pr_head_sha",
            doStepIf=_needs_pr_head_resolve, haltOnFailure=True),
        steps.ShellCommand(
            name="merge-pr", command=merge_pr_command,
            doStepIf=_is_pr_build, haltOnFailure=True),
    ]


@renderer
def _compute_compile_jobs_command(props):
    """Build the `riot_ci/jobs.py` command line from build properties.

    Reads the `boards`, `apps`, `full_build` and `quick_build` build
    properties (all optional) and translates each of them into the matching
    `jobs.py` flag. For PR builds, change detection diffs against the
    checked-out base (`got_revision`); other builds skip change detection.
    """
    cmd = ["python3", ".buildbot/riot_ci/jobs.py"]
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


class ComputeCompileJobs(steps.SetPropertyFromCommand):
    """Runs `riot_ci/jobs.py` on the worker and stores its JSON output as
    the `compile_jobs` build property, for `TriggerCompileJobs` to consume.

    Fails the build, without triggering anything, if `jobs.py` couldn't
    query an application (e.g. a broken Makefile).
    """

    name = "compute-compile-jobs"

    def __init__(self, **kwargs):
        kwargs.setdefault("command", _compute_compile_jobs_command)
        kwargs.setdefault("property", "compile_jobs")
        kwargs.setdefault("haltOnFailure", True)
        super().__init__(**kwargs)


class TriggerCompileJobs(Trigger):
    """Triggers one `compile` build per job in the `compile_jobs` property.

    `compile_jobs` is expected to hold the JSON produced by
    `riot_ci/jobs.py` (typically set by a preceding `ComputeCompileJobs`
    step): `{"jobs": [{"appdir": ..., "board": ..., "toolchain": ...}, ...]}`.
    Each job becomes one triggered build carrying its appdir/board/toolchain
    as build properties, plus `pr_number`/`pr_head_sha` for PR builds. The
    base revision is passed on through the sourcestamp (`updateSourceStamp`),
    so all triggered builds check out the same base as this build.
    """

    def getSchedulersAndProperties(self):
        raw = self.getProperty("compile_jobs") or "{}"
        try:
            data = json.loads(raw) if isinstance(raw, str) else raw
        except json.JSONDecodeError:
            data = {}

        pr_props = {}
        if self.getProperty("pr_number"):
            pr_props = {
                "pr_number": self.getProperty("pr_number"),
                "pr_head_sha": self.getProperty("pr_head_sha"),
            }

        return [
            {
                "sched_name": "trigger-compile",
                "props_to_set": {
                    "appdir": job["appdir"],
                    "board": job["board"],
                    "toolchain": job["toolchain"],
                    **pr_props,
                },
                "unimportant": False,
            }
            for job in data.get("jobs", [])
        ]
