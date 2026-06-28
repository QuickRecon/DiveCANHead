#!/usr/bin/env python3
"""Emit test_manifest.json describing the binary's compile-time configuration.

Consumed by the HIL test rig (divecan_rig) to self-configure and select which
tests are relevant for the flashed binary. Stdlib only — runs in any Zephyr
build environment. Wired as a POST_BUILD step in CMakeLists.txt, or run
standalone against an existing build:

    gen_test_manifest.py \
        --config      build/Firmware/zephyr/.config \
        --dt-header   build/Firmware/zephyr/include/generated/zephyr/devicetree_generated.h \
        --out         build/Firmware/zephyr/test_manifest.json
"""
from __future__ import annotations

import argparse
import json
import re
import sys

DEFAULT_BITRATE = 125000  # DiveCAN is a 125 kbit/s bus

# Battery pack voltages (volts) the rig drives onto BATT_PWR, per chemistry:
#   chemistry -> (nominal, full, low)
# - "low" MIRRORS the firmware's power_math.c power_low_battery_threshold_for()
#   (9V=7.7, LI1S=3.0, LI2S=6.0, LI3S=9.0); keep in sync if that table changes.
# - "nominal"/"full" are the pack's resting / fully-charged voltages.
# The rig runs at nominal by default; explicit HIL tests drive full and low and
# check the voltage the head reports back over DiveCAN.
BATTERY_VOLTAGE = {
    "9V":   (9.0, 9.6, 7.7),
    "LI1S": (3.7, 4.2, 3.5),
    "LI2S": (7.4, 8.4, 6.0),
    "LI3S": (11.1, 12.6, 9.0),
}


def parse_config(path: str) -> dict[str, str]:
    cfg: dict[str, str] = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, val = line.split("=", 1)
            cfg[key] = val.strip().strip('"')
    return cfg


def is_set(cfg, key) -> bool:
    return cfg.get(key) == "y"


def int_cfg(cfg, key, default=-1) -> int:
    try:
        return int(cfg.get(key, default))
    except (TypeError, ValueError):
        return default


def first_set(cfg, options, default="unknown"):
    for name, key in options:
        if is_set(cfg, key):
            return name
    return default


def cell_type(cfg, n) -> str | None:
    for t in ("DIVEO2", "O2S", "ANALOG"):
        if is_set(cfg, f"CONFIG_CELL_{n}_TYPE_{t}"):
            return t
    return None


def dt_can_bitrate(dt_header: str | None) -> int | None:
    if not dt_header:
        return None
    try:
        text = open(dt_header).read()
    except OSError:
        return None
    m = re.search(r"_S_can_[0-9a-fA-F]+_P_bitrate\s+(\d+)", text)
    if not m:
        m = re.search(r"_P_bitrate\s+(\d+)", text)
    return int(m.group(1)) if m else None


def dt_solenoid_channels(dt_header: str | None) -> int:
    """Number of GPIO channels on the `solenoids` devicetree node — i.e. how many
    raw channels solenoid_fire()/the 0xF242 override can drive (4 on the Jr). The
    role->channel map (o2_inject etc.) only advertises *named* roles, so the raw
    count comes from devicetree, not Kconfig."""
    if not dt_header:
        return 0
    try:
        text = open(dt_header).read()
    except OSError:
        return 0
    idxs = re.findall(r"DT_N_S_solenoids_P_gpios_IDX_(\d+)_", text)
    return (max(int(i) for i in idxs) + 1) if idxs else 0


def build_manifest(cfg: dict, dt_header: str | None) -> dict:
    count = int_cfg(cfg, "CONFIG_CELL_COUNT", 0)
    types = [cell_type(cfg, n) for n in range(1, count + 1)]

    bitrate = (dt_can_bitrate(dt_header)
               or int_cfg(cfg, "CONFIG_CAN_DEFAULT_BITRATE", DEFAULT_BITRATE))

    chemistry = first_set(cfg, [
        ("9V", "CONFIG_BATTERY_CHEMISTRY_9V"),
        ("LI1S", "CONFIG_BATTERY_CHEMISTRY_LI1S"),
        ("LI2S", "CONFIG_BATTERY_CHEMISTRY_LI2S"),
        ("LI3S", "CONFIG_BATTERY_CHEMISTRY_LI3S")], default="9V")
    v_nom, v_full, v_low = BATTERY_VOLTAGE.get(chemistry, BATTERY_VOLTAGE["9V"])

    ppo2_default = first_set(cfg, [
        ("PID", "CONFIG_PPO2_CONTROL_DEFAULT_PID"),
        ("MK15", "CONFIG_PPO2_CONTROL_DEFAULT_MK15"),
        ("OFF", "CONFIG_PPO2_CONTROL_DEFAULT_OFF")], default="OFF")

    return {
        "schema_version": 1,
        "source": "kconfig+devicetree",
        "board": cfg.get("CONFIG_BOARD", "unknown"),
        "soc": cfg.get("CONFIG_SOC", "unknown"),
        "can": {"bitrate": bitrate},
        "cells": {
            "count": count,
            "types": types,
            "has": {
                "digital": is_set(cfg, "CONFIG_HAS_DIGITAL_CELL"),
                "analog": is_set(cfg, "CONFIG_HAS_ANALOG_CELL"),
                "diveo2": is_set(cfg, "CONFIG_HAS_DIVEO2_CELL"),
                "o2s": is_set(cfg, "CONFIG_HAS_O2S_CELL"),
            },
        },
        "power": {
            "mode": first_set(cfg, [
                ("BATTERY_THEN_CAN", "CONFIG_POWER_MODE_BATTERY_THEN_CAN"),
                ("CAN", "CONFIG_POWER_MODE_CAN"),
                ("BATTERY", "CONFIG_POWER_MODE_BATTERY")]),
            "battery_chemistry": chemistry,
            # Volts the rig drives onto BATT_PWR. "low" mirrors the firmware's
            # power_math.c threshold for this chemistry.
            "battery_voltage_nominal": v_nom,
            "battery_voltage_full": v_full,
            "battery_voltage_low": v_low,
        },
        "solenoids": {
            "has_o2": is_set(cfg, "CONFIG_HAS_O2_SOLENOID"),
            "has_flush": is_set(cfg, "CONFIG_HAS_FLUSH_SOLENOID"),
            # Raw GPIO channels the driver (and the 0xF242 override) can drive.
            "drivable_channels": dt_solenoid_channels(dt_header),
            # HIL capped raw-fire capability — see writeSolenoidOverrideDID().
            "override": {
                "supported": is_set(cfg, "CONFIG_HAS_O2_SOLENOID"),
                "did": "0xF242",
                "on_time_ms": 1500,
                "requires_programming_session": True,
                "magic": "0x5A",
            },
            "channels": {
                "o2_inject": int_cfg(cfg, "CONFIG_SOL_O2_INJECT_CHANNEL"),
                "o2_inject_2": int_cfg(cfg, "CONFIG_SOL_O2_INJECT_2_CHANNEL"),
                "o2_flush": int_cfg(cfg, "CONFIG_SOL_O2_FLUSH_CHANNEL"),
                "dil_flush": int_cfg(cfg, "CONFIG_SOL_DIL_FLUSH_CHANNEL"),
            },
        },
        "control": {
            "ppo2_default": ppo2_default,
            "solenoid_pid": ppo2_default == "PID",
            # Selectable control algorithms (mirror valid_ppo2_control_modes[],
            # which is gated on a solenoid being present). Switching modes is
            # boot-latched in ppo2_control_init -> the rig must persist + reboot.
            "modes": (["OFF", "PID", "MK15"]
                      if is_set(cfg, "CONFIG_HAS_O2_SOLENOID") else ["OFF"]),
            "mode_switch_requires_reboot": True,
            "cal_default": first_set(cfg, [
                ("DIGITAL_REF", "CONFIG_CAL_MODE_DEFAULT_DIGITAL_REF"),
                ("ABSOLUTE", "CONFIG_CAL_MODE_DEFAULT_ABSOLUTE"),
                ("TOTAL_ABSOLUTE", "CONFIG_CAL_MODE_DEFAULT_TOTAL_ABSOLUTE"),
                ("FLUSH", "CONFIG_CAL_MODE_DEFAULT_FLUSH")], default="ABSOLUTE"),
            # depth_compensation = boot default; depth_comp_capable = the feature
            # exists at all (Kconfig depends on HAS_O2_SOLENOID).
            "depth_comp_capable": is_set(cfg, "CONFIG_HAS_O2_SOLENOID"),
            "depth_compensation": is_set(cfg, "CONFIG_DEPTH_COMPENSATION_DEFAULT"),
        },
        # Bus-level capabilities always built into this firmware (UDS menu/diag,
        # OTA over UDS, the DiveCAN menu). Emitted so HIL `requires_*` markers can
        # gate cleanly and so a future build that gates these flips them to false.
        "features": {
            "uds": True,
            "ota": True,
            "menu": True,
        },
    }


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", required=True, help="path to zephyr/.config")
    ap.add_argument("--dt-header", help="devicetree_generated.h (for CAN bitrate)")
    ap.add_argument("--out", required=True, help="output JSON path")
    args = ap.parse_args(argv)

    cfg = parse_config(args.config)
    manifest = build_manifest(cfg, args.dt_header)
    with open(args.out, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"[gen_test_manifest] wrote {args.out} "
          f"(board={manifest['board']}, cells={manifest['cells']['types']}, "
          f"can={manifest['can']['bitrate']})", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
