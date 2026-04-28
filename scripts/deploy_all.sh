#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"$ROOT_DIR/scripts/build_web_ui.sh"
cd "$ROOT_DIR"
~/.platformio/penv/bin/pio run --target upload "$@"
~/.platformio/penv/bin/pio run --target uploadfs "$@"
