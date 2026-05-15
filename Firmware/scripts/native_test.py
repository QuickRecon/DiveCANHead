#!/usr/bin/env python3
"""
native_test.py — unified runner for native_sim ztest targets.

Each test under `Firmware/tests/<name>/` is built into a centralized location
`Firmware/build-native/<name>/`, keeping the Firmware/ root clean (vs. the
historical `build_test_<name>/` clutter). Mirrors the layout twister would
pick if invoked, but does not require twister itself — which avoids the
libffi.so.7 dependency the NCS toolchain ships.

Usage:
    scripts/native_test.py list
    scripts/native_test.py build <name>...        # one or more
    scripts/native_test.py run <name>...
    scripts/native_test.py build-all
    scripts/native_test.py run-all
    scripts/native_test.py clean [<name>...]      # or all when no name given
    scripts/native_test.py discover-cases <name>  # emit "suite::case|file:line"

Pass --coverage to any of build / run / build-all / run-all / clean to
redirect to ``build-coverage/<name>/`` with gcov instrumentation layered
on top of the test's prj.conf. Aggregation happens via
``scripts/coverage.py report``.
                                                  # for every ZTEST in the test's
                                                  # source — used by the CMake
                                                  # umbrella to set
                                                  # DEF_SOURCE_LINE so the VSCode
                                                  # Test Explorer's "Go to Test"
                                                  # navigates to the right line.

VSCode tasks invoke this; CLAUDE.md documents the day-to-day flow.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Matches ZTEST, ZTEST_F, ZTEST_USER, ZTEST_USER_F — every variant of the
# Zephyr test-case macro that takes (suite, case) as the first two args.
# Explicitly excludes ZTEST_SUITE (which has the same arity but defines a
# suite, not a case — second arg is `NULL` for the setup callback).
ZTEST_PATTERN = re.compile(
    r"^\s*ZTEST(?:_F|_USER|_USER_F)?\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,"
    r"\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)"
)

FIRMWARE_ROOT = Path(__file__).resolve().parents[1]
TESTS_DIR = FIRMWARE_ROOT / "tests"
BUILD_ROOT = FIRMWARE_ROOT / "build-native"
COVERAGE_BUILD_ROOT = FIRMWARE_ROOT / "build-coverage"
COVERAGE_OVERLAY = TESTS_DIR / "coverage.conf"


def _root_for(coverage: bool) -> Path:
    return COVERAGE_BUILD_ROOT if coverage else BUILD_ROOT

NCS_TOOLCHAIN = Path(os.environ.get(
    "NCS", "/home/aren/ncs/toolchains/927563c840"
))
ZEPHYR_SDK = os.environ.get(
    "ZEPHYR_SDK_INSTALL_DIR", "/opt/zephyr-sdk"
)


def discover_tests() -> list[str]:
    """Every directory under tests/ that contains a CMakeLists.txt counts."""
    if not TESTS_DIR.is_dir():
        return []
    return sorted(
        p.name for p in TESTS_DIR.iterdir()
        if p.is_dir() and (p / "CMakeLists.txt").is_file()
    )


def build_env() -> dict[str, str]:
    """Toolchain env for west — matches CLAUDE.md's documented build command."""
    env = os.environ.copy()
    env["PATH"] = f"{NCS_TOOLCHAIN / 'usr/local/bin'}:{env.get('PATH', '')}"
    env["LD_LIBRARY_PATH"] = (
        f"{NCS_TOOLCHAIN / 'usr/local/lib'}:"
        f"{env.get('LD_LIBRARY_PATH', '')}"
    )
    env["ZEPHYR_SDK_INSTALL_DIR"] = ZEPHYR_SDK
    return env


def build_one(name: str, coverage: bool = False) -> int:
    src = TESTS_DIR / name
    if not (src / "CMakeLists.txt").is_file():
        print(f"!! no such test: {name}", file=sys.stderr)
        return 2

    root = _root_for(coverage)
    out = root / name
    out.parent.mkdir(parents=True, exist_ok=True)

    label = "coverage" if coverage else "native"
    print(f"== building {name} ({label}) -> {out.relative_to(FIRMWARE_ROOT)}")

    cmd = [
        "west", "build",
        "-d", str(out),
        "-b", "native_sim",
        str(src),
    ]
    if coverage:
        # Layer the coverage overlay on top of the per-test prj.conf.
        # Zephyr applies prj.conf first, then EXTRA_CONF_FILE entries
        # in order — so this strictly adds CONFIG_COVERAGE=y without
        # disturbing test-specific options.
        cmd += [
            "--",
            f"-DEXTRA_CONF_FILE={COVERAGE_OVERLAY}",
        ]

    proc = subprocess.run(cmd, cwd=FIRMWARE_ROOT, env=build_env())
    return proc.returncode


def run_one(name: str, coverage: bool = False) -> int:
    root = _root_for(coverage)
    binary = root / name / "zephyr" / "zephyr.exe"
    if not binary.is_file():
        print(f"!! binary missing for {name} — build it first", file=sys.stderr)
        return 2

    label = "coverage" if coverage else "native"
    print(f"== running {name} ({label})")
    # cwd=binary's build dir so any cwd-relative artefacts (gcov uses
    # absolute paths from the .gcno, so this is belt-and-braces) land
    # alongside the .gcno files instead of polluting FIRMWARE_ROOT.
    proc = subprocess.run([str(binary)], cwd=binary.parent)
    return proc.returncode


def clean_one(name: str, coverage: bool = False) -> None:
    root = _root_for(coverage)
    out = root / name
    if out.is_dir():
        print(f"== removing {out.relative_to(FIRMWARE_ROOT)}")
        shutil.rmtree(out)


def cmd_list(_args: argparse.Namespace) -> int:
    for name in discover_tests():
        print(name)
    return 0


def cmd_build(args: argparse.Namespace) -> int:
    rc = 0
    for name in args.names:
        rc |= build_one(name, coverage=args.coverage)
    return rc


def cmd_run(args: argparse.Namespace) -> int:
    rc = 0
    for name in args.names:
        rc |= run_one(name, coverage=args.coverage)
    return rc


def cmd_build_all(args: argparse.Namespace) -> int:
    rc = 0
    for name in discover_tests():
        rc |= build_one(name, coverage=args.coverage)
    return rc


def cmd_run_all(args: argparse.Namespace) -> int:
    """Build-then-run each test. Aggregates exit codes — any failure exits non-zero."""
    rc = 0
    for name in discover_tests():
        b = build_one(name, coverage=args.coverage)
        if b != 0:
            rc |= b
            continue
        rc |= run_one(name, coverage=args.coverage)
    return rc


def cmd_discover_cases(args: argparse.Namespace) -> int:
    """Emit `suite::case|file:line` for every ZTEST macro under tests/<name>/.

    The CMake umbrella consumes this to set CTest's DEF_SOURCE_LINE
    property, which CMake Tools reads to populate `TestItem.uri` /
    `TestItem.range` so VSCode's "Go to Test" jumps to the macro line.

    Tests are matched textually — preprocessor #if guards are *not*
    evaluated. That is fine for source-location lookup: if a case is
    compiled out, ztest's `-list` won't include it and the umbrella
    will simply not register a CTest entry for it, even though the
    location lookup table contains a stale entry.
    """
    name = args.name
    src_root = TESTS_DIR / name
    if not src_root.is_dir():
        print(f"!! no such test: {name}", file=sys.stderr)
        return 2

    rc = 0
    for c_file in sorted(src_root.rglob("*.c")):
        try:
            text = c_file.read_text(encoding="utf-8", errors="replace")
        except OSError as exc:
            print(f"!! could not read {c_file}: {exc}", file=sys.stderr)
            rc = 1
            continue
        for line_no, line in enumerate(text.splitlines(), start=1):
            match = ZTEST_PATTERN.match(line)
            if match:
                suite, case = match.group(1), match.group(2)
                # Stable, machine-readable separator. CMake splits on '|'
                # because it never appears in C identifiers or paths.
                print(f"{suite}::{case}|{c_file}:{line_no}")
    return rc


def cmd_clean(args: argparse.Namespace) -> int:
    targets = args.names or discover_tests()
    for name in targets:
        clean_one(name, coverage=args.coverage)
    root = _root_for(args.coverage)
    if not args.names and root.is_dir():
        # If user asked to clean everything, also remove the root if empty.
        try:
            root.rmdir()
        except OSError:
            pass
    return 0


def _add_coverage_flag(p: argparse.ArgumentParser) -> None:
    """`--coverage` reroutes build/run/clean to build-coverage/ and layers
    tests/coverage.conf on the configure command. Off by default so the
    ordinary developer flow is untouched."""
    p.add_argument(
        "--coverage",
        action="store_true",
        help="build/run/clean against build-coverage/ with gcov instrumentation",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("list", help="list every discovered test").set_defaults(func=cmd_list)

    p = sub.add_parser("build", help="build one or more tests")
    p.add_argument("names", nargs="+")
    _add_coverage_flag(p)
    p.set_defaults(func=cmd_build)

    p = sub.add_parser("run", help="run a pre-built binary")
    p.add_argument("names", nargs="+")
    _add_coverage_flag(p)
    p.set_defaults(func=cmd_run)

    p = sub.add_parser("build-all", help="build every test")
    _add_coverage_flag(p)
    p.set_defaults(func=cmd_build_all)

    p = sub.add_parser("run-all", help="build + run every test")
    _add_coverage_flag(p)
    p.set_defaults(func=cmd_run_all)

    p = sub.add_parser("clean", help="remove build dirs (specify names or omit for all)")
    p.add_argument("names", nargs="*")
    _add_coverage_flag(p)
    p.set_defaults(func=cmd_clean)

    p = sub.add_parser(
        "discover-cases",
        help="emit 'suite::case|file:line' for every ZTEST in tests/<name>",
    )
    p.add_argument("name")
    p.set_defaults(func=cmd_discover_cases)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
