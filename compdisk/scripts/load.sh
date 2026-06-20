#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SIZE_MB=${SIZE_MB:-64}
MIN_SAVING=${MIN_SAVING:-10}
SUDO=()
(( EUID == 0 )) || SUDO=(sudo)

[[ -f "$ROOT/compdisk.ko" ]] || "$ROOT/scripts/build.sh"
if lsmod | awk '{print $1}' | grep -qx compdisk; then
    echo "Module compdisk đã được nạp." >&2
    exit 1
fi

"${SUDO[@]}" insmod "$ROOT/compdisk.ko" \
    disk_size_mb="$SIZE_MB" min_saving_percent="$MIN_SAVING"

# udev thường tự tạo node; fallback mknod phục vụ môi trường tối giản.
command -v udevadm >/dev/null && "${SUDO[@]}" udevadm settle || true
for _ in {1..20}; do
    [[ -b /dev/compdisk ]] && break
    sleep 0.1
done
if [[ ! -b /dev/compdisk ]]; then
    major=$(awk '$2 == "compdisk" {print $1}' /proc/devices)
    if [[ -z "$major" ]]; then
        echo "Không tìm thấy major number của compdisk." >&2
        "${SUDO[@]}" rmmod compdisk || true
        exit 1
    fi
    "${SUDO[@]}" mknod /dev/compdisk b "$major" 0
fi
"${SUDO[@]}" chmod 0660 /dev/compdisk

echo "Đã nạp /dev/compdisk: ${SIZE_MB} MiB, min saving ${MIN_SAVING}%"
cat /proc/compdisk_stats
