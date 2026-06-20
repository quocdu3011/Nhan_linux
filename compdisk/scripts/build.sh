#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
KERNEL_RELEASE=${KERNEL_RELEASE:-$(uname -r)}
KDIR=${KDIR:-/lib/modules/$KERNEL_RELEASE/build}

if [[ ! -d "$KDIR" ]]; then
    echo "Không tìm thấy kernel headers tại: $KDIR" >&2
    echo "Cài bằng: sudo apt install linux-headers-$KERNEL_RELEASE" >&2
    exit 1
fi

echo "Kernel: $KERNEL_RELEASE"
echo "Headers: $KDIR"
make -C "$ROOT" clean
make -C "$ROOT" KDIR="$KDIR" W=1
modinfo "$ROOT/compdisk.ko" | sed -n '1,12p'
echo "Build thành công: $ROOT/compdisk.ko"
