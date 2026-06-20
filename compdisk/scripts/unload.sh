#!/usr/bin/env bash
set -euo pipefail

SUDO=()
(( EUID == 0 )) || SUDO=(sudo)

if command -v findmnt >/dev/null && [[ -b /dev/compdisk ]]; then
    while IFS= read -r target; do
        [[ -n "$target" ]] || continue
        echo "Unmount: $target"
        "${SUDO[@]}" umount "$target"
    done < <(findmnt -rn -S /dev/compdisk -o TARGET || true)
fi

if lsmod | awk '{print $1}' | grep -qx compdisk; then
    "${SUDO[@]}" rmmod compdisk
fi

# Chỉ xóa node thủ công còn sót lại sau khi module đã được gỡ.
if [[ -b /dev/compdisk ]]; then
    "${SUDO[@]}" rm -f /dev/compdisk
fi
echo "Đã gỡ compdisk và giải phóng RAM."
