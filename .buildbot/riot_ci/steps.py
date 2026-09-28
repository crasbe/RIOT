"""Buildbot steps that compute and fan out RIOT's compile-job matrix."""
import json

from buildbot.plugins import steps
from buildbot.process.properties import renderer
from buildbot.steps.trigger import Trigger


@renderer
def _compute_compile_jobs_command(props):
    """Build the `riot_ci/jobs.py` command line from build properties.

    Reads the `boards`, `apps`, `full_build`, `quick_build` and
    `base_commit` build properties (all optional) and translates each of
    them into the matching `jobs.py` command-line flag.
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
    base_commit = props.getProperty("base_commit")
    if base_commit:
        cmd += ["--upstream-commit", base_commit]
    return cmd


class ComputeCompileJobs(steps.SetPropertyFromCommand):
    """Runs `riot_ci/jobs.py` on the worker and stores its JSON output as
    the `compile_jobs` build property, for `TriggerCompileJobs` to consume.
    """

    name = "compute-compile-jobs"

    def __init__(self, **kwargs):
        kwargs.setdefault("command", _compute_compile_jobs_command)
        kwargs.setdefault("property", "compile_jobs")
        super().__init__(**kwargs)


class TriggerCompileJobs(Trigger):
    """Triggers one `compile` build per job in the `compile_jobs` property.

    `compile_jobs` is expected to hold the JSON produced by
    `riot_ci/jobs.py` (typically set by a preceding `ComputeCompileJobs`
    step): `{"jobs": [{"appdir": ..., "board": ..., "toolchain": ...}, ...]}`.
    Each job becomes one triggered build on the scheduler named in
    `schedulerNames`, carrying its appdir/board/toolchain as build
    properties.
    """

    def getSchedulersAndProperties(self):
        raw = self.getProperty("compile_jobs") or "{}"
        try:
            data = json.loads(raw) if isinstance(raw, str) else raw
        except json.JSONDecodeError:
            data = {}

        return [
            {
                "sched_name": "trigger-compile",
                "props_to_set": {
                    "appdir": job["appdir"],
                    "board": job["board"],
                    "toolchain": job["toolchain"],
                },
                "unimportant": False,
            }
            for job in data.get("jobs", [])
        ]
