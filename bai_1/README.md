# Bài 1: Linux Administration Toolkit

Bài tập Shell quản trị Linux gồm bốn nhóm chức năng: quản lý file, lập lịch với cron, thiết lập thời gian hệ thống và quản lý phần mềm tự động. Chương trình có menu tiếng Việt và giao diện dòng lệnh để tích hợp vào script khác.

## Cấu trúc thư mục

```text
bai_1/
    ├── linux-admin.sh       # chương trình chính
    ├── README.md            # tài liệu sử dụng
    └── tests/
        ├── test.sh          # bộ kiểm thử
        └── fake-bin/
            └── crontab      # crontab giả lập dùng khi kiểm thử
```

## Yêu cầu

- Bash 4.4 trở lên, hệ điều hành Linux.
- `cron/cronie` cho chức năng lập lịch; `systemd` cho `timedatectl`.
- Một trong các trình quản lý gói: APT, DNF, YUM, Zypper, Pacman hoặc APK.
- `sudo` khi thay đổi thời gian, múi giờ, NTP hoặc phần mềm (không cần nếu chạy bằng root).

## Chạy chương trình

Từ thư mục gốc `Bai_tap_lon`, chuyển vào thư mục bài 1 trước khi chạy:

```bash
cd bai_1
chmod +x linux-admin.sh
./linux-admin.sh             # menu tương tác
./linux-admin.sh help        # toàn bộ cú pháp
```

Ví dụ:

```bash
./linux-admin.sh file list /var/log
./linux-admin.sh file find "$HOME" '*.txt'
./linux-admin.sh file archive backup.tar.gz Documents
./linux-admin.sh schedule add '0 2 * * *' '/home/user/backup.sh' 'backup hằng ngày'
./linux-admin.sh time timezone set Asia/Ho_Chi_Minh
./linux-admin.sh package search nginx
./linux-admin.sh package install nginx
```

Các thao tác xóa, cài/gỡ gói và thay đổi hệ thống luôn hỏi xác nhận. Khi chạy tự động, đặt `--yes` trước nhóm lệnh, ví dụ `./linux-admin.sh --yes package install curl`.

## Chức năng và an toàn

- File: liệt kê, tìm kiếm, tạo thư mục, sao chép, di chuyển, xóa, đổi quyền, nén và giải nén; hỗ trợ tên chứa khoảng trắng.
- Lập lịch: xem crontab, thêm tác vụ có ID, chỉ xóa tác vụ đúng ID do chương trình tạo.
- Thời gian: xem trạng thái, đặt ngày giờ, múi giờ và NTP; kiểm tra định dạng trước khi thay đổi.
- Phần mềm: tự phát hiện bản phân phối và ánh xạ lệnh tìm/cài/gỡ/cập nhật phù hợp.
- Kiểm tra đường dẫn, tên gói, biểu thức cron; chặn xóa `/`, `.`, `..` và chặn path traversal khi giải nén.
- Nhật ký mặc định: `~/.local/state/linux-admin/activity.log`. Có thể đổi bằng biến `LINUX_ADMIN_LOG`.

## Mở rộng

Các nhóm được tách thành `file_command`, `schedule_command`, `time_command` và `package_command`. Để thêm chức năng, bổ sung một nhánh trong hàm tương ứng, cập nhật `usage` và menu. Hàm `run_privileged` tập trung xử lý quyền root; `detect_package_manager` là điểm thêm trình quản lý gói mới.

## Kiểm tra nhanh

Các lệnh sau được thực hiện bên trong thư mục `bai_1`:

```bash
bash -n linux-admin.sh
./linux-admin.sh version
./linux-admin.sh file list .
./tests/test.sh
```

Không nên chạy chương trình bằng root toàn thời gian. Chương trình chỉ yêu cầu nâng quyền đúng lúc thao tác hệ thống.
