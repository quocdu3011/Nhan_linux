# Bài 2: Linux Control Center

Ứng dụng desktop C++ quản lý tiến trình, file, socket và network trên Ubuntu. Giao diện Qt 6 có sidebar, dashboard, thẻ thống kê, bảng tìm kiếm, bo góc và bảng màu tối hiện đại.

## Chức năng

- **Dashboard:** tổng quan số tiến trình, socket, giao diện mạng, bộ nhớ và thông tin hệ thống.
- **Tiến trình:** đọc trực tiếp `/proc`, tìm kiếm, xem PID/user/trạng thái/RAM/CPU time/câu lệnh, gửi `SIGTERM` hoặc `SIGKILL`.
- **File:** duyệt theo cây thư mục bằng nút tam giác, mở bằng double-click, quay lên thư mục cha, tạo thư mục, đổi tên, xóa và xem quyền/kích thước/thời gian sửa. Nhánh cây được tải khi mở; kích thước thư mục được tính đệ quy ở background để không khóa giao diện.
- **Socket:** xem TCP/UDP qua `ss`, lọc kết quả, xem local/peer/process và kết thúc tiến trình sở hữu socket.
- **Network:** đọc interface bằng `getifaddrs`, địa chỉ IPv4/IPv6, trạng thái và lưu lượng RX/TX từ sysfs; bật/tắt interface qua PolicyKit; ping host không khóa giao diện.
- Xác nhận trước thao tác phá hủy, thông báo lỗi rõ ràng và không chạy toàn bộ ứng dụng bằng root.

## Yêu cầu

Ubuntu/Debian với C++17, CMake, Qt 6, Ninja và `iproute2`:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools iproute2 iputils-ping policykit-1
```

## Build và chạy

Từ thư mục gốc bài tập:

```bash
cd bai_2
chmod +x run.sh
./run.sh
```

Hoặc build thủ công:

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/linux-control-center
```

Kiểm tra phiên bản hoặc các tùy chọn terminal:

```bash
./build/linux-control-center --help
./build/linux-control-center --version
```

## Quyền hệ thống

- Xem dữ liệu không cần quyền root.
- Chỉ có thể dừng tiến trình thuộc user hiện tại, trừ khi user có quyền cao hơn.
- Bật/tắt network interface gọi `pkexec ip link set ...`; Ubuntu sẽ hiện hộp thoại xác thực PolicyKit.
- Không chạy ứng dụng bằng `sudo`, vì điều đó làm GUI và file tạo mới thuộc root.

## Cấu trúc

```text
bai_2/
├── CMakeLists.txt
├── linux-control-center.desktop
├── run.sh
├── README.md
└── src/
    └── main.cpp
```

## Kiểm tra

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Mã nguồn tách từng trang thành các hàm dựng/refresh riêng, thuận tiện bổ sung quản lý service, firewall hoặc biểu đồ realtime sau này.
