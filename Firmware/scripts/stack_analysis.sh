#!/usr/bin/env bash
# Run the static stack analyzer against the most recent build.
#
# Usage:
#   scripts/stack_analysis.sh [build_dir]
#
# Outputs a sorted table (largest worst-case stack first) to stdout, plus
# a copy in stackAnalysis.txt at the repo root for diffing across changes.
#
# Pre-requisite: the build must include -fstack-usage and
# -fdump-rtl-dfinish on the TUs of interest. The latter is an opt-in GCC
# analysis flag and is not part of regular firmware or coverage builds.

set -euo pipefail

BUILD_DIR="${1:-build}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"

# readelf — prefer the arm-zephyr-eabi toolchain's binutils so weak/local
# bindings line up with what actually links. Falls back to system readelf.
TOOLCHAIN_READELF="/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-readelf"
if [[ -x "$TOOLCHAIN_READELF" ]]; then
    export READELF="$TOOLCHAIN_READELF"
fi

OUT="$ROOT/stackAnalysis.txt"

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "build dir '$BUILD_DIR' not found — run west build first" >&2
    exit 1
fi

python3 "$SCRIPT_DIR/wcs.py" "$BUILD_DIR" | tee "$OUT"
echo "# Wrote $OUT"
