#!/usr/bin/env bash
# Linux Administration Toolkit - file, scheduler, time and package management.

set -o nounset
set -o pipefail
set -o errexit

readonly APP_NAME="linux-admin"
readonly VERSION="1.0.0"
readonly CRON_TAG="# linux-admin:"
LOG_FILE="${LINUX_ADMIN_LOG:-${XDG_STATE_HOME:-$HOME/.local/state}/linux-admin/activity.log}"
ASSUME_YES=0

COLOR=1
[[ -t 1 && "${NO_COLOR:-}" == "" ]] || COLOR=0
if (( COLOR )); then
    C_RED=$'\033[31m'; C_GREEN=$'\033[32m'; C_YELLOW=$'\033[33m'; C_BLUE=$'\033[34m'; C_RESET=$'\033[0m'
else
    C_RED=""; C_GREEN=""; C_YELLOW=""; C_BLUE=""; C_RESET=""
fi

info()  { printf '%s[INFO]%s %s\n' "$C_BLUE" "$C_RESET" "$*"; }
ok()    { printf '%s[OK]%s %s\n' "$C_GREEN" "$C_RESET" "$*"; }
warn()  { printf '%s[CẢNH BÁO]%s %s\n' "$C_YELLOW" "$C_RESET" "$*" >&2; }
error() { printf '%s[LỖI]%s %s\n' "$C_RED" "$C_RESET" "$*" >&2; }

log_action() {
    local status=$1; shift
    mkdir -p "$(dirname "$LOG_FILE")" 2>/dev/null || return 0
    printf '%(%F %T)T\tuser=%s\tstatus=%s\t%s\n' -1 "${USER:-unknown}" "$status" "$*" >> "$LOG_FILE" 2>/dev/null || true
}

die() { error "$*"; log_action ERROR "$*"; exit 1; }
need_cmd() { command -v "$1" >/dev/null 2>&1 || die "Thiếu lệnh '$1'. Hãy cài đặt trước khi sử dụng."; }

confirm() {
    local answer
    (( ASSUME_YES )) && return 0
    [[ -t 0 ]] || die "Thao tác cần xác nhận. Thêm --yes nếu chạy không tương tác."
    read -r -p "$1 [y/N]: " answer
    [[ "$answer" =~ ^[Yy]$ ]]
}

run_privileged() {
    if (( EUID == 0 )); then "$@"; else need_cmd sudo; sudo -- "$@"; fi
}

usage() {
    cat <<'EOF'
Linux Administration Toolkit

Cách dùng:
  ./linux-admin.sh                         Mở menu tương tác
  ./linux-admin.sh [--yes] <nhóm> <lệnh> [đối số]

Nhóm file:
  file list [đường_dẫn]                    Liệt kê chi tiết
  file find <thư_mục> <mẫu_tên>           Tìm file (vd: '*.log')
  file mkdir <thư_mục>                    Tạo thư mục
  file copy <nguồn> <đích>                Sao chép file/thư mục
  file move <nguồn> <đích>                Di chuyển/đổi tên
  file delete <đường_dẫn>                 Xóa (cần xác nhận)
  file chmod <quyền> <đường_dẫn>          Đổi quyền (vd: 640)
  file archive <đầu_ra.tar.gz> <nguồn...> Nén thành tar.gz
  file extract <file.tar.*> [thư_mục]     Giải nén an toàn vào thư mục

Nhóm lập lịch (crontab của người dùng hiện tại):
  schedule list                            Xem các tác vụ
  schedule add '<biểu_thức_cron>' '<lệnh>' [mô_tả]
  schedule remove <id>                     Xóa tác vụ do công cụ tạo
  schedule examples                        Xem ví dụ biểu thức cron

Nhóm thời gian:
  time show                                Xem giờ và cấu hình hiện tại
  time set 'YYYY-MM-DD HH:MM:SS'           Đặt giờ hệ thống
  time timezone list [từ_khóa]             Liệt kê múi giờ
  time timezone set <Zone/City>            Đặt múi giờ
  time ntp <on|off>                        Bật/tắt đồng bộ NTP

Nhóm phần mềm:
  package manager                          Hiện trình quản lý gói được phát hiện
  package search <tên>                     Tìm gói
  package installed [tên]                  Liệt kê gói đã cài
  package install <gói...>                 Cài gói (cần xác nhận)
  package remove <gói...>                  Gỡ gói (cần xác nhận)
  package update                           Cập nhật chỉ mục gói

Khác: log | help | version. Biến LINUX_ADMIN_LOG đổi vị trí file nhật ký.
EOF
}

file_command() {
    local action=${1:-}; shift || true
    case "$action" in
        list)
            local path=${1:-.}; [[ -e "$path" ]] || die "Không tồn tại: $path"
            ls -lah -- "$path"
            ;;
        find)
            [[ $# -eq 2 ]] || die "Dùng: file find <thư_mục> <mẫu_tên>"
            [[ -d "$1" ]] || die "Không phải thư mục: $1"
            find "$1" -type f -name "$2" -print
            ;;
        mkdir)
            [[ $# -eq 1 ]] || die "Dùng: file mkdir <thư_mục>"
            mkdir -p -- "$1" && ok "Đã tạo: $1"; log_action OK "mkdir $1"
            ;;
        copy)
            [[ $# -eq 2 ]] || die "Dùng: file copy <nguồn> <đích>"
            [[ -e "$1" ]] || die "Không tồn tại: $1"
            cp -a -- "$1" "$2" && ok "Đã sao chép."; log_action OK "copy $1 -> $2"
            ;;
        move)
            [[ $# -eq 2 ]] || die "Dùng: file move <nguồn> <đích>"
            [[ -e "$1" ]] || die "Không tồn tại: $1"
            mv -- "$1" "$2" && ok "Đã di chuyển."; log_action OK "move $1 -> $2"
            ;;
        delete)
            [[ $# -eq 1 ]] || die "Dùng: file delete <đường_dẫn>"
            [[ -e "$1" || -L "$1" ]] || die "Không tồn tại: $1"
            [[ "$1" != "/" && "$1" != "." && "$1" != ".." && -n "$1" ]] || die "Từ chối xóa đường dẫn nguy hiểm."
            confirm "Xóa vĩnh viễn '$1'?" || { warn "Đã hủy."; return 0; }
            rm -rf -- "$1" && ok "Đã xóa."; log_action OK "delete $1"
            ;;
        chmod)
            [[ $# -eq 2 ]] || die "Dùng: file chmod <quyền> <đường_dẫn>"
            [[ "$1" =~ ^[0-7]{3,4}$ ]] || die "Quyền phải ở dạng bát phân, ví dụ 640 hoặc 0755."
            [[ -e "$2" ]] || die "Không tồn tại: $2"
            chmod "$1" -- "$2" && ok "Đã đổi quyền."; log_action OK "chmod $1 $2"
            ;;
        archive)
            [[ $# -ge 2 ]] || die "Dùng: file archive <đầu_ra.tar.gz> <nguồn...>"
            local output=$1; shift
            local item; for item in "$@"; do [[ -e "$item" ]] || die "Không tồn tại: $item"; done
            tar -czf "$output" -- "$@" && ok "Đã tạo: $output"; log_action OK "archive $output"
            ;;
        extract)
            [[ $# -ge 1 && $# -le 2 ]] || die "Dùng: file extract <file.tar.*> [thư_mục]"
            [[ -f "$1" ]] || die "Không tồn tại: $1"
            local archive=$1 destination=${2:-.}
            mkdir -p -- "$destination"
            tar -tf "$archive" >/dev/null || die "File nén không hợp lệ."
            if tar -tf "$archive" | awk '/(^|\/)\.\.($|\/)|^\// {bad=1} END {exit !bad}'; then
                die "File nén chứa đường dẫn không an toàn."
            fi
            tar -xf "$archive" -C "$destination" && ok "Đã giải nén vào: $destination"
            log_action OK "extract $archive -> $destination"
            ;;
        *) die "Lệnh file không hợp lệ. Chạy 'help' để xem hướng dẫn." ;;
    esac
}

cron_current() { crontab -l 2>/dev/null || true; }
schedule_command() {
    local action=${1:-}; shift || true
    case "$action" in
        list)
            need_cmd crontab
            local data; data=$(cron_current)
            [[ -n "$data" ]] && printf '%s\n' "$data" || info "Crontab đang trống."
            ;;
        add)
            need_cmd crontab
            [[ $# -ge 2 && $# -le 3 ]] || die "Dùng: schedule add '<cron>' '<lệnh>' [mô_tả]"
            local expression=$1 command=$2 description=${3:-task} id line current
            [[ "$expression" != *$'\n'* && "$command" != *$'\n'* ]] || die "Không chấp nhận ký tự xuống dòng."
            # Five cron time fields, or supported @shortcut.
            if [[ ! "$expression" =~ ^(@(reboot|yearly|annually|monthly|weekly|daily|midnight|hourly)|([^[:space:]]+[[:space:]]+){4}[^[:space:]]+)$ ]]; then
                die "Biểu thức cron không hợp lệ (cần đúng 5 trường hoặc @reboot/@daily...)."
            fi
            id="$(date +%s)-${RANDOM}"
            description=${description//$'\n'/ }
            line="$expression $command $CRON_TAG$id $description"
            current=$(cron_current)
            { [[ -n "$current" ]] && printf '%s\n' "$current"; printf '%s\n' "$line"; } | crontab -
            ok "Đã thêm tác vụ, ID: $id"; log_action OK "schedule add id=$id"
            ;;
        remove)
            need_cmd crontab
            [[ $# -eq 1 ]] || die "Dùng: schedule remove <id>"
            [[ "$1" =~ ^[A-Za-z0-9._-]+$ ]] || die "ID không hợp lệ."
            local current filtered
            current=$(cron_current)
            grep -Fq "$CRON_TAG$1 " <<< "$current" || die "Không tìm thấy tác vụ ID: $1"
            filtered=$(grep -Fv "$CRON_TAG$1 " <<< "$current" || true)
            printf '%s\n' "$filtered" | crontab -
            ok "Đã xóa tác vụ: $1"; log_action OK "schedule remove id=$1"
            ;;
        examples)
            cat <<'EOF'
*/5 * * * *     mỗi 5 phút       0 * * * *       mỗi giờ
0 2 * * *       02:00 mỗi ngày   0 8 * * 1       08:00 thứ Hai
0 0 1 * *       đầu mỗi tháng    @reboot         sau khi khởi động
EOF
            ;;
        *) die "Lệnh schedule không hợp lệ." ;;
    esac
}

time_command() {
    need_cmd timedatectl
    local action=${1:-show}; shift || true
    case "$action" in
        show) timedatectl status ;;
        set)
            [[ $# -eq 1 ]] || die "Dùng: time set 'YYYY-MM-DD HH:MM:SS'"
            [[ "$1" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}[[:space:]][0-9]{2}:[0-9]{2}:[0-9]{2}$ ]] || die "Sai định dạng thời gian."
            date -d "$1" >/dev/null 2>&1 || die "Ngày giờ không hợp lệ."
            confirm "Đặt giờ hệ thống thành '$1'?" || { warn "Đã hủy."; return 0; }
            run_privileged timedatectl set-time "$1" && ok "Đã đặt thời gian."; log_action OK "time set $1"
            ;;
        timezone)
            local sub=${1:-}; shift || true
            case "$sub" in
                list)
                    if [[ $# -eq 1 ]]; then timedatectl list-timezones | grep -i -- "$1" || true
                    elif [[ $# -eq 0 ]]; then timedatectl list-timezones
                    else die "Dùng: time timezone list [từ_khóa]"; fi
                    ;;
                set)
                    [[ $# -eq 1 ]] || die "Dùng: time timezone set <Zone/City>"
                    timedatectl list-timezones | grep -Fxq -- "$1" || die "Múi giờ không tồn tại: $1"
                    confirm "Đặt múi giờ thành '$1'?" || { warn "Đã hủy."; return 0; }
                    run_privileged timedatectl set-timezone "$1" && ok "Đã đặt múi giờ."; log_action OK "timezone set $1"
                    ;;
                *) die "Dùng: time timezone <list|set>" ;;
            esac
            ;;
        ntp)
            [[ $# -eq 1 && "$1" =~ ^(on|off)$ ]] || die "Dùng: time ntp <on|off>"
            confirm "Chuyển đồng bộ NTP sang '$1'?" || { warn "Đã hủy."; return 0; }
            run_privileged timedatectl set-ntp "$1" && ok "Đã cập nhật NTP."; log_action OK "ntp $1"
            ;;
        *) die "Lệnh time không hợp lệ." ;;
    esac
}

detect_package_manager() {
    local manager
    for manager in apt-get dnf yum zypper pacman apk; do
        command -v "$manager" >/dev/null 2>&1 && { printf '%s\n' "$manager"; return; }
    done
    return 1
}

package_command() {
    local action=${1:-}; shift || true
    local pm; pm=$(detect_package_manager) || die "Không tìm thấy trình quản lý gói được hỗ trợ."
    case "$action" in
        manager) printf '%s\n' "$pm" ;;
        search)
            [[ $# -eq 1 ]] || die "Dùng: package search <tên>"
            case "$pm" in
                apt-get) apt-cache search -- "$1" ;;
                dnf|yum) "$pm" search "$1" ;;
                zypper) zypper search "$1" ;;
                pacman) pacman -Ss "$1" ;;
                apk) apk search "$1" ;;
            esac
            ;;
        installed)
            [[ $# -le 1 ]] || die "Dùng: package installed [tên]"
            local query=${1:-}
            case "$pm" in
                apt-get)
                    if [[ -n "$query" ]]; then dpkg-query -W -f='${binary:Package}\t${Version}\n' "*$query*" 2>/dev/null || true
                    else dpkg-query -W -f='${binary:Package}\t${Version}\n'; fi
                    ;;
                dnf|yum) "$pm" list installed "${query:+*$query*}" ;;
                zypper) zypper search --installed-only "$query" ;;
                pacman)
                    if [[ -n "$query" ]]; then pacman -Q "$query"; else pacman -Q; fi
                    ;;
                apk)
                    if [[ -n "$query" ]]; then apk info -vv "$query"; else apk info -vv; fi
                    ;;
            esac
            ;;
        install|remove)
            [[ $# -ge 1 ]] || die "Dùng: package $action <gói...>"
            local package; for package in "$@"; do [[ "$package" =~ ^[A-Za-z0-9][A-Za-z0-9.+:_@/-]*$ ]] || die "Tên gói không hợp lệ: $package"; done
            confirm "$action các gói: $*?" || { warn "Đã hủy."; return 0; }
            case "$pm:$action" in
                apt-get:install) run_privileged apt-get install -y -- "$@" ;;
                apt-get:remove) run_privileged apt-get remove -y -- "$@" ;;
                dnf:install|yum:install) run_privileged "$pm" install -y "$@" ;;
                dnf:remove|yum:remove) run_privileged "$pm" remove -y "$@" ;;
                zypper:install) run_privileged zypper --non-interactive install "$@" ;;
                zypper:remove) run_privileged zypper --non-interactive remove "$@" ;;
                pacman:install) run_privileged pacman --noconfirm -S "$@" ;;
                pacman:remove) run_privileged pacman --noconfirm -R "$@" ;;
                apk:install) run_privileged apk add "$@" ;;
                apk:remove) run_privileged apk del "$@" ;;
            esac
            ok "Thao tác gói hoàn tất."; log_action OK "package $action $*"
            ;;
        update)
            confirm "Cập nhật chỉ mục/danh sách gói?" || { warn "Đã hủy."; return 0; }
            case "$pm" in
                apt-get) run_privileged apt-get update ;;
                dnf|yum) run_privileged "$pm" makecache ;;
                zypper) run_privileged zypper refresh ;;
                pacman) run_privileged pacman -Sy ;;
                apk) run_privileged apk update ;;
            esac
            ok "Đã cập nhật chỉ mục gói."; log_action OK "package update"
            ;;
        *) die "Lệnh package không hợp lệ." ;;
    esac
}

pause_menu() { read -r -p "Nhấn Enter để tiếp tục..." _ || true; }
interactive_menu() {
    [[ -t 0 ]] || die "Menu cần terminal tương tác. Chạy 'help' để xem chế độ dòng lệnh."
    local choice
    while true; do
        clear 2>/dev/null || true
        printf '%sLinux Administration Toolkit %s%s\n' "$C_BLUE" "$VERSION" "$C_RESET"
        cat <<'EOF'
1. Quản lý file
2. Lập lịch tác vụ
3. Thiết lập thời gian hệ thống
4. Cài đặt/gỡ bỏ chương trình
5. Xem nhật ký
0. Thoát
EOF
        read -r -p "Chọn: " choice
        case "$choice" in
            1) file_menu ;;
            2) schedule_menu ;;
            3) time_menu ;;
            4) package_menu ;;
            5) [[ -f "$LOG_FILE" ]] && tail -n 100 -- "$LOG_FILE" || info "Chưa có nhật ký."; pause_menu ;;
            0) return 0 ;;
            *) warn "Lựa chọn không hợp lệ."; pause_menu ;;
        esac
    done
}

file_menu() {
    local c a b
    printf '1.Liệt kê  2.Tìm  3.Tạo thư mục  4.Sao chép  5.Di chuyển  6.Xóa  7.Đổi quyền  8.Nén  9.Giải nén\n'
    read -r -p "Chọn: " c
    case "$c" in
        1) read -r -p "Đường dẫn [.]: " a; file_command list "${a:-.}" ;;
        2) read -r -p "Thư mục: " a; read -r -p "Mẫu tên: " b; file_command find "$a" "$b" ;;
        3) read -r -p "Thư mục: " a; file_command mkdir "$a" ;;
        4) read -r -p "Nguồn: " a; read -r -p "Đích: " b; file_command copy "$a" "$b" ;;
        5) read -r -p "Nguồn: " a; read -r -p "Đích: " b; file_command move "$a" "$b" ;;
        6) read -r -p "Đường dẫn: " a; file_command delete "$a" ;;
        7) read -r -p "Quyền: " a; read -r -p "Đường dẫn: " b; file_command chmod "$a" "$b" ;;
        8) read -r -p "File đầu ra: " a; read -r -p "Nguồn: " b; file_command archive "$a" "$b" ;;
        9) read -r -p "File nén: " a; read -r -p "Thư mục đích [.]: " b; file_command extract "$a" "${b:-.}" ;;
        *) warn "Không hợp lệ." ;;
    esac
    pause_menu
}

schedule_menu() {
    local c a b d
    printf '1.Xem  2.Thêm  3.Xóa  4.Ví dụ cron\n'; read -r -p "Chọn: " c
    case "$c" in
        1) schedule_command list ;;
        2) read -r -p "Biểu thức cron: " a; read -r -p "Lệnh thực thi: " b; read -r -p "Mô tả: " d; schedule_command add "$a" "$b" "$d" ;;
        3) read -r -p "ID: " a; schedule_command remove "$a" ;;
        4) schedule_command examples ;;
        *) warn "Không hợp lệ." ;;
    esac
    pause_menu
}

time_menu() {
    local c a
    printf '1.Xem  2.Đặt giờ  3.Liệt kê múi giờ  4.Đặt múi giờ  5.Bật NTP  6.Tắt NTP\n'; read -r -p "Chọn: " c
    case "$c" in
        1) time_command show ;;
        2) read -r -p "YYYY-MM-DD HH:MM:SS: " a; time_command set "$a" ;;
        3) read -r -p "Từ khóa lọc (để trống = tất cả): " a; [[ -n "$a" ]] && time_command timezone list "$a" || time_command timezone list ;;
        4) read -r -p "Zone/City: " a; time_command timezone set "$a" ;;
        5) time_command ntp on ;; 6) time_command ntp off ;; *) warn "Không hợp lệ." ;;
    esac
    pause_menu
}

package_menu() {
    local c a
    printf '1.Trình quản lý  2.Tìm  3.Đã cài  4.Cài  5.Gỡ  6.Cập nhật chỉ mục\n'; read -r -p "Chọn: " c
    case "$c" in
        1) package_command manager ;;
        2) read -r -p "Tên gói: " a; package_command search "$a" ;;
        3) read -r -p "Tên cần lọc (để trống = tất cả): " a; [[ -n "$a" ]] && package_command installed "$a" || package_command installed ;;
        4) read -r -p "Tên gói: " a; package_command install "$a" ;;
        5) read -r -p "Tên gói: " a; package_command remove "$a" ;;
        6) package_command update ;; *) warn "Không hợp lệ." ;;
    esac
    pause_menu
}

main() {
    while [[ ${1:-} == --* ]]; do
        case "$1" in --yes) ASSUME_YES=1 ;; --help) usage; return ;; --version) printf '%s %s\n' "$APP_NAME" "$VERSION"; return ;;
            *) die "Tùy chọn không hợp lệ: $1" ;; esac
        shift
    done
    local group=${1:-}; [[ $# -eq 0 ]] || shift
    case "$group" in
        "") interactive_menu ;; file) file_command "$@" ;; schedule) schedule_command "$@" ;;
        time) time_command "$@" ;; package) package_command "$@" ;;
        log) [[ -f "$LOG_FILE" ]] && tail -n 100 -- "$LOG_FILE" || info "Chưa có nhật ký." ;;
        help) usage ;; version) printf '%s %s\n' "$APP_NAME" "$VERSION" ;; *) die "Nhóm lệnh không hợp lệ: $group" ;;
    esac
}

main "$@"
