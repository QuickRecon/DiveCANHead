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
from pathlib import Path

DEFAULT_BITRATE = 125000  # DiveCAN is a 125 kbit/s bus


def _validated_path(raw: str, *, must_exist: bool = False) -> Path:
    """Resolve a CLI-supplied path and reject obviously malformed input.

    This script is invoked with trusted build-system paths (CMake
    POST_BUILD, ad-hoc dev runs) — absolute paths and arbitrary build
    directories are legal, so this is a sanity check, not a jail. It
    guards every filesystem access in this script against a mangled
    argument (empty string, embedded NUL) before the path reaches
    open()/read_text(), and — when must_exist=True — surfaces a missing
    input as a clear FileNotFoundError instead of a bare open() failure.
    """
    if not raw or "\0" in raw:
        raise ValueError(f"invalid path argument: {raw!r}")
    resolved = Path(raw).resolve(strict=must_exist)
    if not str(resolved):
        raise ValueError(f"invalid path argument: {raw!r}")
    return resolved


# Canonical settings STORAGE order + labels. This MIRRORS the settings[] table and
# the SETTING_INDEX_* / BUILD_ASSERT storage-index lock in
# src/divecan/uds/uds_settings.c — keep in sync if that table changes. The cell-
# broadcast block is fixed at CELL_MAX_COUNT (== 3, BUILD_ASSERT-locked), NOT the
# per-variant CELL_COUNT, so every build exposes C1/C2/C3 Bcst. The HIL rig uses
# this + the CONFIG_MENU_ORDER_* permutation below to know the WIRE order the
# handset/UDS presents, so its label-index assumptions never silently go stale
# when a variant reorders its menu (e.g. eCCR surfacing Battery in a menu slot).
_SETTING_STORAGE_LABELS = [
    "FW Commit",   # 0  SETTING_INDEX_FW_COMMIT
    "PPO2 Mode",   # 1  SETTING_INDEX_PPO2_MODE
    "Cal Mode",    # 2  SETTING_INDEX_CAL_MODE
    "DepthComp",   # 3  SETTING_INDEX_DEPTH_COMP
    "Kp x1M",      # 4  SETTING_INDEX_PID_KP
    "Ki x1M",      # 5  SETTING_INDEX_PID_KI
    "Kd x1M",      # 6  SETTING_INDEX_PID_KD
    "Battery",     # 7  SETTING_INDEX_BATTERY_TYPE
    "C1 Bcst",     # 8  SETTING_INDEX_CELL_BCST_BASE + 0
    "C2 Bcst",     # 9  + 1
    "C3 Bcst",     # 10 + 2 (CELL_MAX_COUNT == 3)
]
# Number of handset menu-order slots (CONFIG_MENU_ORDER_1..N). Mirrors
# MENU_CONFIG_SLOTS in uds_settings.c.
_MENU_CONFIG_SLOTS = 5


def compute_menu_order(menu_cfg, setting_count):
    """Wire (handset-facing) index -> storage index permutation. A direct mirror of
    UDS_ComputeMenuOrder() in uds_settings.c: take the curated leading slots
    (valid, in range, de-duplicated), then append every remaining setting in
    natural storage order so nothing is hidden from UDS. Returns a list of storage
    indices whose position is the wire index."""
    order = []
    for s in menu_cfg:
        if 0 <= s < setting_count and s not in order:
            order.append(s)
    for s in range(setting_count):
        if s not in order:
            order.append(s)
    return order


def settings_block(cfg) -> dict:
    """The settings menu contract: storage order + the per-variant wire order the
    handset/UDS actually presents (permuted by CONFIG_MENU_ORDER_*). Emitted so the
    HIL rig resolves setting indices by LABEL from the flashed binary's own menu
    order instead of hard-coding wire indices that a variant can reorder."""
    labels = list(_SETTING_STORAGE_LABELS)
    count = len(labels)
    # CONFIG_MENU_ORDER_n defaults mirror the Kconfig defaults / the C #ifndef
    # fallbacks (identity for the leading slots: slot n -> storage n-1).
    menu_cfg = [int_cfg(cfg, f"CONFIG_MENU_ORDER_{i + 1}", i)
                for i in range(_MENU_CONFIG_SLOTS)]
    wire_order = compute_menu_order(menu_cfg, count)
    return {
        "count": count,
        "menu_order_config": menu_cfg,
        "storage_labels": labels,
        # wire index -> storage index, and the label at each wire index.
        "wire_order": wire_order,
        "wire_labels": [labels[s] for s in wire_order],
    }

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
    validated = _validated_path(path, must_exist=True)
    with open(validated) as f:
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


def str_list_cfg(cfg, key):
    """A string Kconfig holding a space/comma-separated name list -> list."""
    raw = cfg.get(key, "") or ""
    return [t for t in raw.replace(",", " ").split() if t]


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
        text = _validated_path(dt_header, must_exist=True).read_text()
    except (OSError, ValueError):
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
        text = _validated_path(dt_header, must_exist=True).read_text()
    except (OSError, ValueError):
        return 0
    idxs = re.findall(r"DT_N_S_solenoids_P_gpios_IDX_(\d+)_", text)
    return (max(int(i) for i in idxs) + 1) if idxs else 0


def build_manifest(cfg: dict, dt_header: str | None, variant: str = "unknown") -> dict:
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
        "variant": variant,
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
            # Zero when the solenoid driver is compiled out (CONFIG_SOLENOID=n),
            # even though the devicetree node may still list GPIOs.
            "drivable_channels": (dt_solenoid_channels(dt_header)
                                  if is_set(cfg, "CONFIG_SOLENOID") else 0),
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
            # Setpoint-change flush burst on-time (ms). 0 = feature disabled
            # (CONFIG_SOL_FLUSH_TIME). When >0, a diver-commanded setpoint
            # increase fires the O2 flush solenoid and a decrease the dil flush
            # solenoid for this duration at the start of the next fire cycle.
            "flush_on_time_ms": int_cfg(cfg, "CONFIG_SOL_FLUSH_TIME", 0),
            # True when a secondary O2 inject solenoid is wired: the fire thread
            # alternates inject fires between the primary and secondary valves.
            "dual_o2_inject": int_cfg(cfg, "CONFIG_SOL_O2_INJECT_2_CHANNEL") >= 0,
            # Closed-loop current check: the driver samples the generic
            # device-current API around each fire and classifies the draw against
            # this delta window (microamps). Thresholds are variant-configurable
            # (drivers/solenoid/Kconfig); the HIL test reads them from here so it
            # asserts against the exact bounds the flashed binary uses.
            "current_check": {
                "enabled": is_set(cfg, "CONFIG_SOLENOID_CURRENT_CHECK"),
                "delta_min_ua": int_cfg(cfg,
                                        "CONFIG_SOLENOID_CURRENT_DELTA_MIN_UA", 0),
                "delta_max_ua": int_cfg(cfg,
                                        "CONFIG_SOLENOID_CURRENT_DELTA_MAX_UA", 0),
                "fault_streak": int_cfg(cfg,
                                        "CONFIG_SOLENOID_CURRENT_FAULT_STREAK", 0),
                # DS2782 sense resistor (micro-ohms) the provider uses for
                # counts->uA. The HIL feedback needs it to invert uA->counts so
                # the value round-trips to the same uA the firmware compares.
                "shunt_uohm": int_cfg(cfg, "CONFIG_POSEIDON_DS2782_SHUNT_UOHM", 0),
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
        # Settings menu contract: storage order + the per-variant WIRE order the
        # handset/UDS presents (permuted by CONFIG_MENU_ORDER_*). The rig resolves
        # setting indices by label from here, so a variant reordering its menu
        # (e.g. eCCR surfacing Battery) never silently misdirects a fixed-index write.
        "settings": settings_block(cfg),
        # Bus-level capabilities always built into this firmware (UDS menu/diag,
        # OTA over UDS, the DiveCAN menu). Emitted so HIL `requires_*` markers can
        # gate cleanly and so a future build that gates these flips them to false.
        "features": {
            "uds": True,
            "ota": True,
            "menu": True,
            "poseidon_accessories": is_set(cfg, "CONFIG_POSEIDON_ACCESSORIES"),
        },
        # CAN log-push backend tuning. force_inf_modules mirrors
        # CONFIG_LOG_PUSH_FORCE_INF_MODULES (space/comma-separated module names
        # forwarded up to INF regardless of the runtime verbosity) so the HIL
        # can gate its force-INF stream assertion on builds that elevate the
        # module it stimulates.
        "log_push": {
            "force_inf_modules": str_list_cfg(
                cfg, "CONFIG_LOG_PUSH_FORCE_INF_MODULES"),
        },
        "poseidon": {
            "enabled": is_set(cfg, "CONFIG_POSEIDON_ACCESSORIES"),
            "hud_address": 0x40,
            "display_address": 0x41,
            "battery_address": 0x43,
            "fuel_gauge_stale_ms": 12000,
        },
        # HP tank pressure transducers on the analog-cell ADC inputs. Per-cylinder
        # linear map (min_mv -> 0 bar, max_mv -> limit_bar); a channel of -1 means
        # that cylinder's transducer is not configured. The rig drives the ADC
        # channel with the matching CellSim and predicts the TANK_PRESSURE_ID
        # decibar broadcast. stale_ms mirrors TANK_PRESSURE_STALE_MS in
        # divecan_ppo2_tx.c.
        "transducer": {
            "enabled": is_set(cfg, "CONFIG_HAS_PRESSURE_TRANSDUCER"),
            "o2": {
                "channel": int_cfg(cfg, "CONFIG_O2_TRANSDUCER_CHANNEL"),
                "min_mv": int_cfg(cfg, "CONFIG_O2_TRANSDUCER_MIN"),
                "max_mv": int_cfg(cfg, "CONFIG_O2_TRANSDUCER_MAX"),
                "limit_bar": int_cfg(cfg, "CONFIG_O2_TRANSDUCER_LIMIT"),
            },
            "dil": {
                "channel": int_cfg(cfg, "CONFIG_DIL_TRANSDUCER_CHANNEL"),
                "min_mv": int_cfg(cfg, "CONFIG_DIL_TRANSDUCER_MIN"),
                "max_mv": int_cfg(cfg, "CONFIG_DIL_TRANSDUCER_MAX"),
                "limit_bar": int_cfg(cfg, "CONFIG_DIL_TRANSDUCER_LIMIT"),
            },
            "stale_ms": 3000,
        },
    }


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", required=True, help="path to zephyr/.config")
    ap.add_argument("--dt-header", help="devicetree_generated.h (for CAN bitrate)")
    ap.add_argument("--variant", default="unknown",
                    help="build variant name (from the selected variants/<name>.conf); "
                         "the HIL rig asserts the flashed firmware reports this same name")
    ap.add_argument("--out", required=True, help="output JSON path")
    args = ap.parse_args(argv)

    cfg = parse_config(args.config)
    manifest = build_manifest(cfg, args.dt_header, args.variant)
    out_path = _validated_path(args.out)
    with open(out_path, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"[gen_test_manifest] wrote {args.out} "
          f"(variant={manifest['variant']}, board={manifest['board']}, "
          f"cells={manifest['cells']['types']}, "
          f"can={manifest['can']['bitrate']})", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
