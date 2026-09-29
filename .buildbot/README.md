# RIOT CI on Buildbot

Buildbot configuration for building RIOT applications across their supported
boards and toolchains. Kept next to the source it builds so CI config changes
are reviewed and tracked together with the code. It is loaded by the
`buildbot-dispatcher` container, which checks out this directory from RIOT
on every start.

## Layout

- `master.cfg` — the Buildbot config: workers, schedulers, builders, web UI.
- `riot_ci/jobs.py` — computes the (application, board, toolchain)
  compile-job matrix. Runs on a worker: it shells out to `make` and to
  `dist/tools/ci/can_fast_ci_run.py` to figure out which apps/boards need
  building. See its module docstring and function docstrings for details.
- `riot_ci/steps.py` — `checkout_steps()` fetches and merges the code under
  test; the other steps run `jobs.py` on the workers and trigger the builds
  for the job matrix (see "Job model").

`dist/tools/compile_like_buildbot/` runs the same job matrix locally with
plain `make`, without any Buildbot setup.

## Checkout

Every build fetches RIOT fresh. The base is the branch the build was started
for. With the `pr_number` property set, the PR's head is merged into that base
with `git merge --no-ff`, the same way GitHub would merge it; a PR that
doesn't merge cleanly fails in the `coordinator`, before any compile build
starts. Triggered builds receive the coordinator's exact base SHA and PR head
SHA, so every build of one run tests the identical merge, even if the base
branch moves in the meantime.

The checkouts borrow their git objects (`git clone --reference`) from
git-cache's mirror of the repository on the worker's persistent `/cache`
volume, so they only hold the files and can live in RAM. The mirror is
fetched by the `coordinator` and whenever it lacks the commit a build needs.

## Job model

A CI run starts with a `coordinator` build and fans out over the workers:

1. `coordinator`: checks out (and merges) the code, decides which
   applications to build and for which boards (`jobs.py --select-only`:
   change detection, quick-build board selection), and triggers one
   `list-jobs` build per batch of applications (`BUILDBOT_APPS_PER_BATCH`,
   default 25). It waits for them, so it fails if any batch does.
2. `list-jobs`: queries its applications for their supported
   (board, toolchain) combinations — the slow part, now spread over the
   workers — and triggers one `compile` build per combination. Compile
   builds therefore start while other batches are still being listed.
3. `compile`: builds one application for one board and toolchain.

Every worker is attached to every builder; Buildbot hands each build to
whichever worker is free next. A worker runs one `list-jobs`/`compile`/`test`
build at a time; the lightweight `coordinator` runs next to it. A static
builder per app/board combination isn't an option, since that matrix is only
known once RIOT is checked out.

## Web interface

Readable by everyone. Forcing, stopping or rebuilding requires logging in as
`admin` with the password from `BUILDBOT_WWW_ADMIN_PASSWORD`; without that
variable, the web interface is read-only.

## Current scope

- **No change source**: builds are started through the `force` scheduler
  (web UI or REST API), with the base branch and an optional PR number. A
  GitHub webhook and status reporting are not wired up yet.
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

Needs `BUILDBOT_WORKERS` (space-separated `name:password` pairs) in the
environment, e.g. `BUILDBOT_WORKERS="w1:x" buildbot checkconfig .buildbot`;
without any worker, the builders have nothing to run on and the check fails.
