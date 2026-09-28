# RIOT CI on Buildbot

Buildbot master configuration for building RIOT applications across their
supported boards and toolchains. Kept next to the source it builds so CI
config changes are reviewed and tracked together with the code.

## Layout

- `master.cfg` — the master config: workers, schedulers, builders.
- `riot_ci/jobs.py` — computes the (application, board, toolchain)
  compile-job matrix. Runs on a worker: it shells out to `make` and to
  `dist/tools/ci/can_fast_ci_run.py` to figure out which apps/boards need
  building. See its module docstring and function docstrings for details.
- `riot_ci/steps.py` — `ComputeCompileJobs` runs `jobs.py` on the worker and
  captures its JSON output as a build property; `TriggerCompileJobs` reads
  that property and triggers one `compile` build per job.

## Job model

One shared `compile` builder, with every worker attached to it, fed by the
`trigger-compile` scheduler. Each triggered build carries its own
`appdir`/`board`/`toolchain` properties, so Buildbot hands jobs out to
whichever worker is free next. A static `Builder` per app/board combination
isn't an option here, since that matrix is only known once RIOT is checked
out.

## Current scope

- **No change source**: builds are started manually through the `force`
  scheduler. A GitHub push/PR change source and status reporting are not
  wired up yet.
- **`master.cfg` only**: no Dockerfile/compose/DB setup for the master
  itself. `c["db"]["db_url"]` defaults to a local sqlite file, which is
  enough for `buildbot checkconfig` and local testing.
- **`test` builder is unused**: it's scaffolded for dispatching a build's
  test run to a hardware board, but nothing triggers it yet — that
  assignment still needs to be designed. Right now, only native32/native64
  run their test inline, right after compiling, on the same worker.

## Local checkconfig

```sh
python3 -m venv .venv && . .venv/bin/activate
pip install -r .buildbot/requirements.txt
buildbot checkconfig .buildbot
```

Needs `BUILDBOT_WORKER_NAMES` (and one `BUILDBOT_WORKER_<NAME>_PASSWORD` per
name) in the environment, or the worker list is simply empty — the config
still loads.
