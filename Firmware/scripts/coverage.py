#!/usr/bin/env python3
"""
coverage.py — combined-coverage pipeline for native_sim builds.

Orchestrates three things and stitches their .gcda traces into one report:

  1. Every ztest module under ``tests/<name>/`` (via native_test.py's
     ``--coverage`` mode). Builds + runs each into build-coverage/<name>/.
  2. The integration firmware (``tests/integration/integration.conf``,
     which carries the full test topology). Build only — the pytest
     harness drives the run.
  3. The pytest harness under ``tests/integration/harness/``. Points
     ``DIVECAN_FW_BIN`` at the instrumented integration binary so every
     fixture launch produces .gcda files alongside the .gcno files.

gcovr then walks ``build-coverage/`` for paired .gcno/.gcda artefacts and
produces an HTML report (``coverage-report/index.html``) plus a SonarQube
generic-coverage XML at ``coverage-report/coverage.xml``.

Subcommands:

    scripts/coverage.py build-tests          # build every ztest with --coverage
    scripts/coverage.py build-integration    # build the integration firmware
    scripts/coverage.py build                # both of the above
    scripts/coverage.py run-tests            # execute the ztest binaries
    scripts/coverage.py run-pytest [extra…]  # pytest with DIVECAN_FW_BIN set
    scripts/coverage.py report               # gcovr → coverage-report/
    scripts/coverage.py all [pytest_args…]   # build → run-tests → run-pytest → report
    scripts/coverage.py clean                # wipe build-coverage/ + coverage-report/

.gcda files are written by libgcov's atexit hook, so any clean exit
(ztest's posix_exit, the pytest fixture's SIGTERM teardown) flushes the
counters. SIGKILL or k_panic skips the flush, so the harness already
gives SIGTERM a grace window before escalating.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# Reuse native_test.py's build_env() and test discovery so we don't drift.
SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
import native_test  # noqa: E402

FIRMWARE_ROOT = native_test.FIRMWARE_ROOT
TESTS_DIR = native_test.TESTS_DIR
COVERAGE_BUILD_ROOT = native_test.COVERAGE_BUILD_ROOT
COVERAGE_OVERLAY = native_test.COVERAGE_OVERLAY

INTEGRATION_BUILD = COVERAGE_BUILD_ROOT / "integration"
INTEGRATION_BIN = INTEGRATION_BUILD / "zephyr" / "zephyr.exe"
REPORT_DIR = FIRMWARE_ROOT / "coverage-report"
HARNESS_DIR = TESTS_DIR / "integration" / "harness"

# Sources we measure. Everything else (Zephyr kernel, NCS modules,
# tests/, drivers/gpio_sim/, proprietary/) is excluded from the report.
# COVERAGE_FILTER already restricts to src/, so the closed-source out-of-tree
# module under proprietary/ is outside scope; it is listed explicitly too as
# belt-and-braces.
COVERAGE_FILTER = str(FIRMWARE_ROOT / "src")
COVERAGE_EXCLUDES = [
    str(FIRMWARE_ROOT / "tests"),
    str(FIRMWARE_ROOT / "drivers" / "gpio_sim"),
    str(FIRMWARE_ROOT / "proprietary"),
]


def _run(cmd: list[str], cwd: Path | None = None,
         env: dict[str, str] | None = None) -> int:
    pretty = " ".join(str(c) for c in cmd)
    print(f"== {pretty}")
    return subprocess.run(cmd, cwd=cwd, env=env).returncode


def cmd_build_tests(_args: argparse.Namespace) -> int:
    rc = 0
    for name in native_test.discover_tests():
        rc |= native_test.build_one(name, coverage=True)
    return rc


def cmd_build_integration(_args: argparse.Namespace) -> int:
    """Build the integration native_sim firmware with gcov instrumentation.

    Mirrors the canonical integration build command from
    ``tests/integration/SANITIZERS.md`` but targets
    ``build-coverage/integration/`` and stacks the coverage overlay on top
    of the self-contained integration config.
    """
    INTEGRATION_BUILD.parent.mkdir(parents=True, exist_ok=True)
    user_cache = COVERAGE_BUILD_ROOT / ".zephyr-cache" / "integration"
    (user_cache / "ToolchainCapabilityDatabase").mkdir(
        parents=True, exist_ok=True
    )

    extra_conf = ";".join([
        "tests/integration/integration.conf",
        str(COVERAGE_OVERLAY.relative_to(FIRMWARE_ROOT)),
    ])

    cmd = [
        "west", "build",
        "-d", str(INTEGRATION_BUILD),
        "-b", native_test.NATIVE_BOARD,
        str(FIRMWARE_ROOT),
        "--",
        f"-DUSER_CACHE_DIR={user_cache}",
        "-DBOARD_ROOT=.",
        "-DDTC_OVERLAY_FILE=tests/integration/boards/native_sim.overlay",
        f"-DEXTRA_CONF_FILE={extra_conf}",
    ]
    return _run(cmd, cwd=FIRMWARE_ROOT, env=native_test.build_env())


def cmd_build(args: argparse.Namespace) -> int:
    rc = cmd_build_tests(args)
    rc |= cmd_build_integration(args)
    return rc


def cmd_run_tests(_args: argparse.Namespace) -> int:
    """Run every ztest binary that was built with --coverage.

    Skips any module that wasn't built into ``build-coverage/`` (so partial
    builds are tolerated). Returns non-zero if *any* binary failed — a test
    failure still produces .gcda for that binary because libgcov's exit
    hook runs before the non-zero exit propagates.
    """
    rc = 0
    for name in native_test.discover_tests():
        binary = COVERAGE_BUILD_ROOT / name / "zephyr" / "zephyr.exe"
        if not binary.is_file():
            print(f"-- skipping {name} (no coverage build)")
            continue
        rc |= native_test.run_one(name, coverage=True)
    return rc


def cmd_run_pytest(args: argparse.Namespace) -> int:
    """Run the pytest integration suite against the coverage build.

    Prefers the harness's bundled ``.venv/bin/pytest`` so python-can and
    matplotlib resolve correctly. Falls back to ``pytest`` on PATH if the
    venv is missing — let the user fix the import error themselves rather
    than masking it.
    """
    if not INTEGRATION_BIN.is_file():
        print(f"!! integration binary missing at {INTEGRATION_BIN} — "
              "run 'scripts/coverage.py build-integration' first",
              file=sys.stderr)
        return 2

    env = os.environ.copy()
    env["DIVECAN_FW_BIN"] = str(INTEGRATION_BIN)
    # Give libgcov's atexit hook time to flush every .gcda before
    # conftest escalates to SIGKILL. 5 s is generous — a clean flush
    # typically takes ~100 ms even for the full firmware build — but
    # cheap insurance against truncated traces.
    env.setdefault("DIVECAN_TERMINATE_GRACE_S", "5.0")
    # Cap rt_ratio markers under coverage. --coverage + -fno-inline
    # roughly doubles per-instruction wall time, so a test that asks
    # for --rt-ratio=100 only achieves ~50× — but the harness still
    # expects responses within the original wall-time deadlines, which
    # then trip. Clamping to 10× keeps the firmware well ahead of
    # wall time without overwhelming it.
    env.setdefault("DIVECAN_RT_RATIO_MAX", "10")
    # test_pwr_management asserts the firmware exits within
    # SHUTDOWN_DEADLINE_S after a CAN shutdown request. The flush hook
    # injects ~1–2 s between sys_reboot and the final posix_exit, and
    # the abort window now runs at 10× instead of 100× — widen the
    # deadline so both effects fit.
    env.setdefault("DIVECAN_SHUTDOWN_DEADLINE_S", "15.0")

    venv_pytest = HARNESS_DIR / ".venv" / "bin" / "pytest"
    pytest_cmd = str(venv_pytest) if venv_pytest.is_file() else "pytest"

    cmd = [pytest_cmd, *args.extra]
    return _run(cmd, cwd=HARNESS_DIR, env=env)


def prefix_sonar_report_paths(report_path: Path) -> None:
    """Make generic coverage paths relative to the repository scan root."""
    tree = ET.parse(report_path)
    for file_element in tree.getroot().findall("file"):
        source_path = Path(file_element.attrib["path"])
        if not source_path.is_absolute():
            file_element.set(
                "path",
                (Path(FIRMWARE_ROOT.name) / source_path).as_posix(),
            )
    tree.write(report_path, encoding="utf-8", xml_declaration=True)


def cmd_report(_args: argparse.Namespace) -> int:
    """Aggregate every .gcda under build-coverage/ into a single report."""
    if shutil.which("gcovr") is None:
        print("!! gcovr not on PATH — install it (pacman -S gcovr / pip install gcovr)",
              file=sys.stderr)
        return 2

    if not COVERAGE_BUILD_ROOT.is_dir():
        print(f"!! {COVERAGE_BUILD_ROOT} does not exist — nothing to report",
              file=sys.stderr)
        return 2

    REPORT_DIR.mkdir(parents=True, exist_ok=True)

    # `--root` anchors the report at the firmware tree so paths in the
    # output are relative to Firmware/. `--filter` restricts to app code
    # (src/), `--exclude` strips the test harnesses and gpio_sim shim
    # which would otherwise muddy the percentage.
    # search_paths is positional in gcovr's argument layout, so the
    # build-coverage tree goes at the end of the command line.
    #
    # --merge-mode-functions=merge-use-line-min: several functions in
    # this codebase (e.g. calibration.c's per-cell cal_compute_coeff_cell_N)
    # are defined inside #ifdef CONFIG_CELL_N_TYPE_{ANALOG,DIVEO2,O2S}
    # branches. Different builds (integration uses one cell mix, ztests
    # may build a different one) land the same function name on
    # different source lines. Default 'strict' mode refuses to merge
    # those; 'merge-use-line-min' attributes the function to the
    # earliest definition, which is consistent and good enough for the
    # report.
    def gcovr_base(root: Path) -> list[str]:
        cmd = [
            "gcovr",
            "--root", str(root),
            "--filter", COVERAGE_FILTER,
            "--gcov-ignore-parse-errors",
            "--merge-mode-functions=merge-use-line-min",
            "--print-summary",
        ]
        gcov_executable = os.environ.get("GCOV_EXECUTABLE")
        if gcov_executable:
            cmd += ["--gcov-executable", gcov_executable]
        for ex in COVERAGE_EXCLUDES:
            cmd += ["--exclude", ex]
        return cmd

    # gcov needs Firmware/ as its root to reconstruct each compiler working
    # directory correctly. Keep that root for both reports, then prefix the
    # generic XML paths for the repository-root Sonar scan.
    html_cmd = gcovr_base(FIRMWARE_ROOT) + [
        "--html-details", str(REPORT_DIR / "index.html"),
        str(COVERAGE_BUILD_ROOT),
    ]
    xml_cmd = gcovr_base(FIRMWARE_ROOT) + [
        "--sonarqube", str(REPORT_DIR / "coverage.xml"),
        str(COVERAGE_BUILD_ROOT),
    ]

    html_rc = _run(html_cmd, cwd=FIRMWARE_ROOT)
    xml_rc = _run(xml_cmd, cwd=FIRMWARE_ROOT)
    if xml_rc == 0:
        prefix_sonar_report_paths(REPORT_DIR / "coverage.xml")
    rc = html_rc | xml_rc
    if rc == 0:
        print(f"\n== report: {REPORT_DIR / 'index.html'}")
        print(f"== sonar:  {REPORT_DIR / 'coverage.xml'}")
    return rc


def cmd_all(args: argparse.Namespace) -> int:
    """build → run-tests → run-pytest → report, short-circuiting on build fail.

    Test failures during run-tests/run-pytest do not stop the pipeline —
    coverage of failing paths is still useful, and the final report will
    note the missing flushes if any.
    """
    rc = cmd_build_tests(args)
    if rc != 0:
        print("!! test builds failed; aborting", file=sys.stderr)
        return rc

    rc = cmd_build_integration(args)
    if rc != 0:
        print("!! integration build failed; aborting", file=sys.stderr)
        return rc

    cmd_run_tests(args)
    cmd_run_pytest(args)
    return cmd_report(args)


def cmd_clean(_args: argparse.Namespace) -> int:
    for path in (COVERAGE_BUILD_ROOT, REPORT_DIR):
        if path.is_dir():
            print(f"== removing {path.relative_to(FIRMWARE_ROOT)}")
            shutil.rmtree(path)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("build-tests",
                   help="build every ztest binary with coverage").set_defaults(
                       func=cmd_build_tests)
    sub.add_parser("build-integration",
                   help="build the integration firmware with coverage").set_defaults(
                       func=cmd_build_integration)
    sub.add_parser("build",
                   help="build-tests + build-integration").set_defaults(
                       func=cmd_build)

    sub.add_parser("run-tests",
                   help="run every built ztest binary").set_defaults(
                       func=cmd_run_tests)

    p = sub.add_parser("run-pytest",
                       help="run pytest harness against the coverage build")
    p.add_argument("extra", nargs=argparse.REMAINDER,
                   help="extra args forwarded to pytest")
    p.set_defaults(func=cmd_run_pytest)

    sub.add_parser("report",
                   help="aggregate .gcda into coverage-report/").set_defaults(
                       func=cmd_report)

    p = sub.add_parser("all",
                       help="build → run-tests → run-pytest → report")
    p.add_argument("extra", nargs=argparse.REMAINDER,
                   help="extra args forwarded to pytest")
    p.set_defaults(func=cmd_all)

    sub.add_parser("clean",
                   help="remove build-coverage/ and coverage-report/").set_defaults(
                       func=cmd_clean)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
