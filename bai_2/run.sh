#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cmake -S "$ROOT" -B "$ROOT/build" -G Ninja
cmake --build "$ROOT/build"
exec "$ROOT/build/linux-control-center"
