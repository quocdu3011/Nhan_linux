#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
APP="$ROOT/linux-admin.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export LINUX_ADMIN_LOG="$TMP/activity.log"
export NO_COLOR=1
export FAKE_CRONTAB_FILE="$TMP/crontab"

fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
assert_file() { [[ -f "$1" ]] || fail "Không tìm thấy file $1"; }
assert_dir() { [[ -d "$1" ]] || fail "Không tìm thấy thư mục $1"; }

bash -n "$APP"
[[ $($APP version) == "linux-admin 1.0.0" ]] || fail "Sai phiên bản"

$APP file mkdir "$TMP/source/sub"
assert_dir "$TMP/source/sub"
printf 'hello\n' > "$TMP/source/file with space.txt"
$APP file copy "$TMP/source/file with space.txt" "$TMP/copied.txt"
assert_file "$TMP/copied.txt"
[[ $($APP file find "$TMP" '*.txt' | wc -l) -eq 2 ]] || fail "Tìm file không đúng"
$APP file chmod 640 "$TMP/copied.txt"
[[ $(stat -c '%a' "$TMP/copied.txt") == 640 ]] || fail "chmod không đúng"
$APP file move "$TMP/copied.txt" "$TMP/moved.txt"
assert_file "$TMP/moved.txt"

(
    cd "$TMP"
    "$APP" file archive data.tar.gz source
    "$APP" file extract data.tar.gz restored
)
assert_file "$TMP/restored/source/file with space.txt"

$APP --yes file delete "$TMP/moved.txt"
[[ ! -e "$TMP/moved.txt" ]] || fail "Xóa file thất bại"
if $APP --yes file delete / >/dev/null 2>&1; then fail "Không chặn xóa /"; fi
if $APP file chmod 999 "$TMP/source" >/dev/null 2>&1; then fail "Không chặn quyền sai"; fi

PATH="$ROOT/tests/fake-bin:$PATH" $APP schedule add '0 2 * * *' '/usr/bin/true' 'test task' >/dev/null
grep -Fq '# linux-admin:' "$FAKE_CRONTAB_FILE" || fail "Không thêm được cron"
cron_id=$(sed -n 's/.*# linux-admin:\([^ ]*\).*/\1/p' "$FAKE_CRONTAB_FILE")
[[ -n "$cron_id" ]] || fail "Cron không có ID"
PATH="$ROOT/tests/fake-bin:$PATH" $APP schedule remove "$cron_id" >/dev/null
if grep -Fq '# linux-admin:' "$FAKE_CRONTAB_FILE"; then fail "Không xóa được cron theo ID"; fi
if PATH="$ROOT/tests/fake-bin:$PATH" $APP schedule add 'bad cron' true >/dev/null 2>&1; then fail "Không chặn cron sai"; fi

$APP package manager >/dev/null
assert_file "$LINUX_ADMIN_LOG"
printf 'PASS: tất cả kiểm tra an toàn đã đạt.\n'
