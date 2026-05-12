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


def build_one(name: str) -> int:
    src = TESTS_DIR / name
    if not (src / "CMakeLists.txt").is_file():
        print(f"!! no such test: {name}", file=sys.stderr)
        return 2

    out = BUILD_ROOT / name
    out.parent.mkdir(parents=True, exist_ok=True)

    print(f"== building {name} -> {out.relative_to(FIRMWARE_ROOT)}")
    proc = subprocess.run(
        [
            "west", "build",
            "-d", str(out),
            "-b", "native_sim",
            str(src),
        ],
        cwd=FIRMWARE_ROOT,
        env=build_env(),
    )
    return proc.returncode


def run_one(name: str) -> int:
    binary = BUILD_ROOT / name / "zephyr" / "zephyr.exe"
    if not binary.is_file():
        print(f"!! binary missing for {name} — build it first", file=sys.stderr)
        return 2

    print(f"== running {name}")
    proc = subprocess.run([str(binary)], cwd=FIRMWARE_ROOT)
    return proc.returncode


def clean_one(name: str) -> None:
    out = BUILD_ROOT / name
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
        rc |= build_one(name)
    return rc


def cmd_run(args: argparse.Namespace) -> int:
    rc = 0
    for name in args.names:
        rc |= run_one(name)
    return rc


def cmd_build_all(_args: argparse.Namespace) -> int:
    rc = 0
    for name in discover_tests():
        rc |= build_one(name)
    return rc


def cmd_run_all(_args: argparse.Namespace) -> int:
    """Build-then-run each test. Aggregates exit codes — any failure exits non-zero."""
    rc = 0
    for name in discover_tests():
        b = build_one(name)
        if b != 0:
            rc |= b
            continue
        rc |= run_one(name)
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
        clean_one(name)
    if not args.names and BUILD_ROOT.is_dir():
        # If user asked to clean everything, also remove the root if empty.
        try:
            BUILD_ROOT.rmdir()
        except OSError:
            pass
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("list", help="list every discovered test").set_defaults(func=cmd_list)

    p = sub.add_parser("build", help="build one or more tests")
    p.add_argument("names", nargs="+")
    p.set_defaults(func=cmd_build)

    p = sub.add_parser("run", help="run a pre-built binary")
    p.add_argument("names", nargs="+")
    p.set_defaults(func=cmd_run)

    sub.add_parser("build-all", help="build every test").set_defaults(func=cmd_build_all)
    sub.add_parser("run-all", help="build + run every test").set_defaults(func=cmd_run_all)

    p = sub.add_parser("clean", help="remove build dirs (specify names or omit for all)")
    p.add_argument("names", nargs="*")
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
