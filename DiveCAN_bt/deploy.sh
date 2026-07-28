#!/usr/bin/env bash
# Deploy the DiveCAN_bt web tool to the CDN host.
#
# Pushes examples/ and src/ to aren@192.168.1.123:~/docker/cdn/src/ so the
# diagnostics UI is served from ~/docker/cdn/src/examples/diagnostics.html
# and its relative imports (../src/...) resolve to ~/docker/cdn/src/src/.
set -euo pipefail

REMOTE="aren@192.168.1.123"
DEST="docker/cdn/src"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

rsync -avz --delete \
    --exclude='*.test.js' \
    "$HERE/examples/" "$REMOTE:$DEST/examples/"

rsync -avz --delete \
    --exclude='*.test.js' \
    "$HERE/src/" "$REMOTE:$DEST/src/"

echo "Deployed. Diagnostics UI: <cdn-base-url>/examples/diagnostics.html"
