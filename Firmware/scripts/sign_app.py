#!/usr/bin/env python3
"""Manual-invocation wrapper around imgtool sign.

Sysbuild auto-signs the app on every build (see build/Firmware/zephyr/
zephyr.signed.bin). This helper exists for the cases sysbuild doesn't
cover:

  * Re-signing an arbitrary .bin with a different version (e.g. when
    generating a v1 / v2 / v3 pair for OTA negative testing).
  * Producing a *.signed.confirmed.bin — image with the "image_ok"
    flag pre-set so MCUBoot skips the test/confirm cycle when the
    image is flashed directly over SWD. Useful for bench bring-up
    where the POST handset gate hasn't been wired yet.

Sign args (header size, slot size, alignment) are pulled from the app
build's .config so the script can't drift from sysbuild.

Example:
    scripts/sign_app.py build/Firmware/zephyr/zephyr.bin
    scripts/sign_app.py --version 1.2.3+4 --confirm path/to/app.bin
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

FIRMWARE_DIR = Path(__file__).resolve().parent.parent
DEFAULT_CONFIG = FIRMWARE_DIR / "build" / "Firmware" / "zephyr" / ".config"
DEFAULT_IMGTOOL = (
    FIRMWARE_DIR.parent / ".west-projects" / "bootloader" / "mcuboot"
    / "scripts" / "imgtool.py"
)


def _imgtool_python() -> str:
    """Use the interpreter behind the caller-selected West environment."""
    override = os.environ.get("DIVECAN_IMGTOOL_PYTHON")
    if override:
        return override

    west = shutil.which("west")
    if west:
        try:
            first_line = Path(west).read_text(
                encoding="utf-8", errors="replace"
            ).splitlines()[0]
            if first_line.startswith("#!"):
                command = first_line[2:].strip().split()
                if command and Path(command[0]).name == "env" and 1 < len(command):
                    resolved = shutil.which(command[1])
                    if resolved:
                        return resolved
                elif command and Path(command[0]).is_file():
                    return command[0]
        except (OSError, IndexError):
            pass

    return sys.executable


def _validated_path(raw: str, *, must_exist: bool = False) -> Path:
    """Resolve a CLI-supplied path and reject obviously malformed input.

    This script is invoked with trusted build-tree paths (dev CLI use,
    tests/integration/harness/uds_ota.py) — absolute paths and arbitrary
    build dirs are legal, so this is a sanity check ahead of the
    filesystem access, not a jail.
    """
    if not raw or "\0" in raw:
        raise ValueError(f"invalid path argument: {raw!r}")
    resolved = Path(raw).resolve(strict=must_exist)
    if not str(resolved):
        raise ValueError(f"invalid path argument: {raw!r}")
    return resolved


def read_kconfig_int(config_path: Path, key: str) -> int:
    pattern = re.compile(rf"^{re.escape(key)}=(.+)$")
    validated = _validated_path(str(config_path), must_exist=True)
    with validated.open() as f:
        for line in f:
            m = pattern.match(line)
            if m:
                value = m.group(1).strip()
                return int(value, 0)
    raise KeyError(f"{key} not found in {config_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input_bin", type=Path, help="Unsigned app .bin")
    parser.add_argument("--version", default="0.0.0+0",
                        help="Image version (default: 0.0.0+0)")
    parser.add_argument("--align", type=int, default=8,
                        help="Write alignment (default: 8, matches sysbuild)")
    parser.add_argument("--confirm", action="store_true",
                        help="Also emit a *.confirmed.bin with image-ok set")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG,
                        help="App build .config (for header / slot sizes)")
    parser.add_argument("--imgtool", type=Path, default=DEFAULT_IMGTOOL)
    parser.add_argument("--output", type=Path,
                        help="Output .signed.bin path (default: alongside input)")
    args = parser.parse_args()

    if not args.input_bin.is_file():
        print(f"input not found: {args.input_bin}", file=sys.stderr)
        return 1
    if not args.config.is_file():
        print(f".config not found: {args.config} — build the app first",
              file=sys.stderr)
        return 1
    if not args.imgtool.is_file():
        print(f"imgtool.py not found at {args.imgtool}", file=sys.stderr)
        return 1

    header_size = read_kconfig_int(args.config, "CONFIG_ROM_START_OFFSET")
    slot_size = read_kconfig_int(args.config, "CONFIG_FLASH_LOAD_SIZE")

    output = args.output or args.input_bin.with_suffix(".signed.bin")

    env = os.environ.copy()
    python = _imgtool_python()

    def run_sign(out_path: Path, confirm: bool) -> int:
        cmd = [
            python, str(args.imgtool), "sign",
            "--version", args.version,
            "--header-size", hex(header_size),
            "--slot-size", str(slot_size),
            "--align", str(args.align),
        ]
        if confirm:
            cmd.append("--confirm")
        cmd += [str(args.input_bin), str(out_path)]
        print(" ".join(cmd))
        return subprocess.call(cmd, env=env)

    rc = run_sign(output, confirm=False)
    if 0 != rc:
        return rc

    if args.confirm:
        confirmed = output.with_suffix(".confirmed.bin")
        rc = run_sign(confirmed, confirm=True)
        if 0 != rc:
            return rc

    return 0


if __name__ == "__main__":
    sys.exit(main())
