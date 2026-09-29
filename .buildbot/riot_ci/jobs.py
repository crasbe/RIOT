#!/usr/bin/env python3
"""Compute RIOT's compile-job matrix and print it as JSON.

Meant to run on a worker, inside a RIOT checkout: it shells out to `make`
and to `dist/tools/ci/can_fast_ci_run.py` to enumerate which (application,
board, toolchain) combinations need to be built. Invoked as a Buildbot step
by riot_ci/steps.py, which captures its stdout as a build property.

Example:
    python3 jobs.py --riotbase . --apps "examples/basic/hello-world" \\
        --boards "native32 native64" > jobs.json
"""
import argparse
import json
import os
import subprocess
import sys

# Representative board subset used for quick builds instead of the full
# board matrix.
QUICKBUILD_BOARDS = [
    "adafruit-itsybitsy-m4",
    "atmega256rfr2-xpro",
    "esp32-wroom-32",
    "esp32s3-devkit",
    "frdm-k64f",
    "hifive1b",
    "msb-430",
    "msba2",
    "native32",
    "native64",
    "nrf52840dk",
    "qn9080dk",
    "samr21-xpro",
    "stk3200",
    "stm32f429i-disc1",
]

# Boards that are additionally compiled with the LLVM toolchain (on top of
# GNU, which is always tried).
TEST_BOARDS_LLVM_COMPILE = [
    "iotlab-m3",
    "native32",
    "native64",
    "nrf52dk",
    "mulle",
    "nucleo-f401re",
    "samr21-xpro",
    "slstk3402a",
]


def _run(cmd, cwd, env=None):
    return subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True)


def get_apps(riotbase, apps_filter=None):
    """List RIOT's application directories.

    Args:
        riotbase: Path to the RIOT checkout.
        apps_filter: If given, restrict the result to entries also present
            in this iterable.

    Returns:
        A sorted list of application directory paths, relative to `riotbase`
        (e.g. "examples/basic/hello-world").
    """
    result = _run(
        ["make", "--no-print-directory", "-f", "makefiles/app_dirs.inc.mk", "info-applications"],
        cwd=riotbase,
    )
    apps = sorted(line for line in result.stdout.splitlines() if line.strip())
    if apps_filter:
        apps = [a for a in apps if a in apps_filter]
    return apps


def get_supported_boards(riotbase, appdir, boards_filter=None):
    """List the boards a given application supports.

    Args:
        riotbase: Path to the RIOT checkout.
        appdir: Application directory, relative to `riotbase`.
        boards_filter: If given, restrict the result to entries also present
            in this iterable.

    Returns:
        A list of board names, or None if `make info-boards-supported`
        failed for this application (e.g. a broken Makefile).
    """
    env = None
    if boards_filter:
        # Only evaluate these boards instead of all ~300; much faster.
        env = dict(os.environ, BOARDS=" ".join(boards_filter))
    result = _run(
        ["make", "--no-print-directory", "-j2", "info-boards-supported"],
        cwd=os.path.join(riotbase, appdir),
        env=env,
    )
    if result.returncode != 0:
        return None
    boards = result.stdout.split()
    if boards_filter:
        boards = [b for b in boards if b in boards_filter]
    return boards


def get_supported_toolchains(riotbase, appdir, board):
    """List the toolchains a given (application, board) pair should be built with.

    GNU is always included. LLVM is added if `board` is in
    TEST_BOARDS_LLVM_COMPILE and the application reports LLVM support for it.

    Args:
        riotbase: Path to the RIOT checkout.
        appdir: Application directory, relative to `riotbase`.
        board: Board name.

    Returns:
        A list containing "gnu" and, optionally, "llvm".
    """
    toolchains = ["gnu"]
    if board in TEST_BOARDS_LLVM_COMPILE:
        result = _run(
            ["make", "-s", "--no-print-directory", f"BOARD={board}", "info-toolchains-supported"],
            cwd=os.path.join(riotbase, appdir),
        )
        if "llvm" in result.stdout.split():
            toolchains.append("llvm")
    return toolchains


def get_app_board_toolchain_pairs(riotbase, appdir, boards_filter=None):
    """Enumerate the (board, toolchain) combinations to build an application for.

    Args:
        riotbase: Path to the RIOT checkout.
        appdir: Application directory, relative to `riotbase`.
        boards_filter: If given, restrict the boards considered to entries
            also present in this iterable.

    Returns:
        A list of {"appdir", "board", "toolchain"} dicts, or None if
        `get_supported_boards` failed for this application.
    """
    boards = get_supported_boards(riotbase, appdir, boards_filter)
    if boards is None:
        return None
    return [
        {"appdir": appdir, "board": board, "toolchain": toolchain}
        for board in boards
        for toolchain in get_supported_toolchains(riotbase, appdir, board)
    ]


def can_fast_ci_run(riotbase, upstream_commit):
    """Determine which apps/boards changed relative to `upstream_commit`.

    Wraps `dist/tools/ci/can_fast_ci_run.py`, which classifies the diff
    between HEAD and `upstream_commit` to figure out whether a full build is
    required, or only a subset of apps/boards needs rebuilding.

    Args:
        riotbase: Path to the RIOT checkout.
        upstream_commit: Branch or commit to diff against.

    Returns:
        A tuple (apps_changed, boards_changed, full_build_required), where
        the first two are lists of names and the third is a bool.
    """
    result = _run(
        [
            os.path.join(riotbase, "dist/tools/ci/can_fast_ci_run.py"),
            "--riotbase", riotbase,
            "--upstreambranch", upstream_commit,
            "--changed-boards", "--changed-apps", "--json",
        ],
        cwd=riotbase,
    )
    full_build_required = result.returncode != 0
    try:
        data, _ = json.JSONDecoder().raw_decode(result.stdout)
    except (json.JSONDecodeError, ValueError):
        return [], [], True
    return data.get("apps", []), data.get("boards", []), full_build_required


def compute_compile_jobs(riotbase, boards=None, apps=None, full_build=False,
                          quick_build=False, upstream_commit=None):
    """Compute the full compile-job matrix for a RIOT checkout.

    Reports progress on stderr, one line per application.

    Args:
        riotbase: Path to the RIOT checkout.
        boards: Space-free iterable of board names to restrict the build to.
            Takes precedence over change detection and `quick_build`.
        apps: Iterable of application directories to restrict the build to.
            Takes precedence over change detection.
        full_build: If True, skip change detection and build everything
            (subject to `boards`/`apps`).
        quick_build: If no explicit `boards` are given and change detection
            doesn't apply, restrict boards to QUICKBUILD_BOARDS.
        upstream_commit: Branch or commit to diff against for change
            detection. Required for change detection to run at all; without
            it (and without `full_build`/`boards`/`apps`), everything is
            built.

    Returns:
        {"jobs": [...], "errors": [...]}, where "jobs" is a list of
        {"appdir", "board", "toolchain"} dicts and "errors" lists the
        application directories whose supported-boards query failed.
    """
    boards_changed = []
    if not full_build and not boards and not apps and upstream_commit:
        apps_changed, boards_changed, needs_full = can_fast_ci_run(riotbase, upstream_commit)
        if needs_full:
            full_build = True
        elif not apps_changed and not boards_changed:
            return {"jobs": [], "errors": []}
        elif not apps:
            apps = apps_changed or None

    board_filter = boards or boards_changed or (QUICKBUILD_BOARDS if quick_build else None)

    app_list = get_apps(riotbase, apps_filter=apps)
    total = len(app_list)

    jobs = []
    errors = []
    for done, appdir in enumerate(app_list, 1):
        pairs = get_app_board_toolchain_pairs(riotbase, appdir, board_filter)
        if pairs is None:
            errors.append(f"get_supported_boards failed in {appdir}")
            print(f"[{done}/{total}] {appdir}: FAILED", file=sys.stderr, flush=True)
            continue
        jobs.extend(pairs)
        print(f"[{done}/{total}] {appdir}: {len(pairs)} jobs", file=sys.stderr, flush=True)
    return {"jobs": jobs, "errors": errors}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--riotbase", default=os.getcwd(), help="path to the RIOT checkout")
    parser.add_argument("--boards", default=None, help="space-separated board list")
    parser.add_argument("--apps", default=None, help="space-separated app directory list")
    parser.add_argument("--full-build", action="store_true",
                         help="build everything, skip change detection")
    parser.add_argument("--quick-build", action="store_true",
                         help="restrict to QUICKBUILD_BOARDS when no boards/apps are given")
    parser.add_argument("--upstream-commit", default=None,
                         help="branch/commit to diff against for change detection")

    args = parser.parse_args(argv)
    result = compute_compile_jobs(
        args.riotbase,
        boards=args.boards.split() if args.boards else None,
        apps=args.apps.split() if args.apps else None,
        full_build=args.full_build,
        quick_build=args.quick_build,
        upstream_commit=args.upstream_commit,
    )
    json.dump(result, sys.stdout)
    print()
    for error in result["errors"]:
        print(f"error: {error}", file=sys.stderr)
    return 1 if result["errors"] else 0


if __name__ == "__main__":
    sys.exit(main())
