#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
MOUNT_POINT=${MOUNT_POINT:-/mnt/compdisk}
SUDO=()
(( EUID == 0 )) || SUDO=(sudo)

for command in make insmod rmmod mkfs.ext4 mount umount mountpoint dd cmp sha256sum; do
    command -v "$command" >/dev/null || {
        echo "Thiếu lệnh bắt buộc: $command" >&2
        exit 1
    }
done

if (( EUID != 0 )); then
    # Xác thực một lần trước khi thay đổi trạng thái; tránh PASS giả nếu sudo lỗi.
    sudo -v
fi

# Chạy cleanup trong subshell để `set +e` không làm tắt fail-fast của test.
cleanup() (
    set +e
    if mountpoint -q "$MOUNT_POINT"; then "${SUDO[@]}" umount "$MOUNT_POINT"; fi
    if lsmod | awk '{print $1}' | grep -qx compdisk; then "${SUDO[@]}" rmmod compdisk; fi
    [[ -b /dev/compdisk ]] && "${SUDO[@]}" rm -f /dev/compdisk
    true
)
trap cleanup EXIT

cleanup
"$ROOT/scripts/build.sh"
SIZE_MB=${SIZE_MB:-64} MIN_SAVING=${MIN_SAVING:-10} "$ROOT/scripts/load.sh"

"${SUDO[@]}" mkfs.ext4 -F -q /dev/compdisk
"${SUDO[@]}" mkdir -p "$MOUNT_POINT"
"${SUDO[@]}" mount /dev/compdisk "$MOUNT_POINT"

echo "[1/4] Ghi 10 MiB dữ liệu zero (dễ nén)"
"${SUDO[@]}" dd if=/dev/zero of="$MOUNT_POINT/zero.bin" bs=1M count=10 status=progress

echo "[2/4] Ghi 10 MiB dữ liệu random (khó nén)"
"${SUDO[@]}" dd if=/dev/urandom of="$MOUNT_POINT/random.bin" bs=1M count=10 status=progress
sync
hash_before=$("${SUDO[@]}" sha256sum "$MOUNT_POINT/random.bin" | awk '{print $1}')

echo "[3/4] Unmount/mount lại và kiểm tra dữ liệu đọc lại"
"${SUDO[@]}" umount "$MOUNT_POINT"
"${SUDO[@]}" mount /dev/compdisk "$MOUNT_POINT"
"${SUDO[@]}" cmp -n $((10 * 1024 * 1024)) /dev/zero "$MOUNT_POINT/zero.bin"
hash_after=$("${SUDO[@]}" sha256sum "$MOUNT_POINT/random.bin" | awk '{print $1}')
[[ "$hash_before" == "$hash_after" ]]

echo "[4/4] Thống kê nén"
cat /proc/compdisk_stats
grep -Eq '^Compressed blocks: [1-9][0-9]*$' /proc/compdisk_stats
grep -Eq '^Raw blocks: [1-9][0-9]*$' /proc/compdisk_stats

"${SUDO[@]}" umount "$MOUNT_POINT"
"${SUDO[@]}" rmmod compdisk
trap - EXIT
[[ -b /dev/compdisk ]] && "${SUDO[@]}" rm -f /dev/compdisk
echo "PASS: format, mount, zero/random write, read-back và adaptive compression đều đạt."
