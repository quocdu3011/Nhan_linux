# CompDisk — thiết bị khối RAM có nén LZO

CompDisk là Linux kernel module tạo `/dev/compdisk`, một block device có dung
lượng logic mặc định 64 MiB. Dữ liệu chỉ tồn tại trong RAM. Mỗi block logic
4096 byte được thử nén độc lập bằng LZO khi ghi và được giải nén khi đọc.
Thiết bị dùng được với `mkfs.ext4`, `mount`, `cp`, `dd`, `cat` và `rm` như một
ổ đĩa thông thường.

> Cảnh báo: unload module hoặc tắt máy sẽ làm mất toàn bộ dữ liệu. Đây là mô
> hình phục vụ học tập, không phải storage production.

## Yêu cầu và phạm vi tương thích

- Ubuntu với kernel headers khớp kernel đang chạy;
- `make`, GCC, `kmod`, `e2fsprogs` và quyền `sudo` để nạp module;
- kernel Ubuntu hiện đại 5.15+ hoặc 6.x;
- kernel phải bật block layer, procfs và LZO (`CONFIG_LZO_COMPRESS` /
  `CONFIG_LZO_DECOMPRESS`, thường có sẵn trên Ubuntu).

Code có nhánh tương thích API: `proc_ops` từ 5.6 và chữ ký mới của
`blk_alloc_disk()` từ 6.9. Bản này đã build với `W=1` trên Ubuntu kernel
6.17.0-35-generic.

Cài công cụ trên Ubuntu nếu còn thiếu:

```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r) kmod e2fsprogs
```

## Kiến trúc

```text
ext4 / ứng dụng
      │ BIO read, write, flush, discard, write-zeroes
      ▼
Linux block layer → compdisk_submit_bio()
      │
      ▼
ordered workqueue (ngữ cảnh được phép sleep)
      │
      ├─ WRITE → read-modify-write block → LZO → compressed hoặc raw
      ├─ READ  → block chưa ghi: zero; raw: copy; compressed: LZO decode
      └─ DISCARD block đầy đủ → free payload
      │
      ▼
metadata[] + payload cấp phát động trong RAM
```

`struct compdisk_block` giữ con trỏ payload, kích thước, trạng thái nén và lý
do lưu raw. Metadata được cấp một lần cho toàn bộ đĩa; payload chỉ được cấp khi
block được ghi. Một mutex bảo vệ metadata, scratch buffer và thống kê. Ordered
workqueue giữ thứ tự I/O, đồng thời tránh chạy LZO hay cấp phát có thể sleep
ngay trong callback `submit_bio`.

Thiết kế chi tiết nằm tại [docs/architecture.md](docs/architecture.md).

## Luồng ghi

```text
BIO WRITE
  → kiểm tra position + length trong capacity
  → duyệt từng bio_vec
  → chia theo biên block 4096 byte
  → ghi một phần: đọc/giải nén block cũ trước
  → nén thử bằng lzo1x_1_compress
  → saving >= min_saving_percent và compressed < raw?
       có    → lưu compressed payload
       không → lưu đúng 4096 byte raw
  → cập nhật metadata/thống kê → bio_endio
```

Payload mới được cấp phát thành công trước khi thay payload cũ. Vì vậy lỗi
`-ENOMEM` không làm mất nội dung cũ của block.

## Luồng đọc

```text
BIO READ
  → kiểm tra phạm vi
  → với từng block liên quan:
       data == NULL → tạo block zero
       compressed   → lzo1x_decompress_safe, bắt buộc output = 4096
       raw          → memcpy
  → copy phần được yêu cầu vào các page của BIO → bio_endio
```

BIO vượt capacity bị kết thúc bằng lỗi I/O. Dữ liệu nén hỏng cũng trả `-EIO`,
không copy dữ liệu không hợp lệ cho caller.

## Build

```bash
cd compdisk
./scripts/build.sh
```

Hoặc:

```bash
make
make check       # build với W=1
make clean
```

Có thể chỉ định headers khác bằng `KDIR=/path/to/kernel/build` hoặc
`KERNEL_RELEASE=... ./scripts/build.sh`.

## Nạp và sử dụng

```bash
cd compdisk
./scripts/load.sh
sudo mkfs.ext4 -F /dev/compdisk
sudo mkdir -p /mnt/compdisk
sudo mount /dev/compdisk /mnt/compdisk
sudo cp mot-file /mnt/compdisk/
cat /proc/compdisk_stats
./scripts/unload.sh
```

`load.sh` chờ udev tạo device node; nếu hệ thống tối giản không có udev, script
lấy major trong `/proc/devices` và dùng `mknod`.

Module parameters:

```bash
# Đĩa logic 128 MiB; chỉ lưu compressed khi tiết kiệm ít nhất 20%
SIZE_MB=128 MIN_SAVING=20 ./scripts/load.sh

# Tương đương lệnh thấp hơn
sudo insmod compdisk.ko disk_size_mb=128 min_saving_percent=20
```

- `disk_size_mb`: 1..4096, mặc định 64;
- `min_saving_percent`: 0..99, mặc định 10. Dù đặt 0, payload nén vẫn phải nhỏ
  hơn payload raw.

Không được `rmmod` khi filesystem còn mount. Dùng `scripts/unload.sh` để
unmount tất cả mount target của `/dev/compdisk` trước khi gỡ module.

## Kiểm thử tự động

```bash
cd compdisk
./scripts/test.sh
```

Script tự build, load, format ext4, mount, ghi 10 MiB zero và 10 MiB random,
sync, remount, so sánh zero và checksum random, kiểm tra có cả block compressed
lẫn raw, rồi unmount/rmmod. Có thể đổi mount point bằng
`MOUNT_POINT=/path`.

Do test gọi `insmod`, `mkfs`, `mount` và `rmmod`, nó cần sudo. Cleanup trap sẽ
cố gắng unmount và gỡ module nếu một bước thất bại.

## `/proc/compdisk_stats`

Ví dụ:

```text
CompDisk Statistics
Logical size: 64 MB
Block size: 4096 bytes
Total blocks: 16384
Written blocks: 5200
Compressed blocks: 2600
Raw blocks: 2600
Blocks skipped (insufficient saving): 2600
Compression failures: 0
Minimum saving threshold: 10%
Logical used: 21299200 bytes
Physical used: 10700000 bytes
Compression ratio: 1.99
Memory saved: 49.7%
Read requests: 120
Write requests: 100
Discard requests: 3
```

- `Written blocks`: block đang có payload;
- `Compressed blocks` / `Raw blocks`: trạng thái hiện tại, tổng bằng written;
- `Blocks skipped`: block raw hiện tại vì nén không đủ ngưỡng;
- `Compression failures`: số lần gọi LZO thất bại, là counter tích lũy;
- `Logical used`: written blocks × 4096;
- `Physical used`: tổng số byte payload raw/compressed hiện tại;
- `Compression ratio`: logical used / physical used;
- `Memory saved`: phần trăm payload tiết kiệm so với logical used;
- request counters: số BIO theo loại, không phải số block.

`Physical used` chỉ tính payload. Nó không bao gồm metadata, scratch buffers,
work item và allocator overhead, nên không phải tổng footprint chính xác của
module trong kernel.

## Xử lý lỗi và giải phóng

Init dùng chuỗi rollback ngược thứ tự khi bất kỳ bước cấp phát, đăng ký major,
tạo procfs hoặc add disk thất bại. Exit xóa procfs, xóa gendisk, drain/destroy
workqueue, unregister major và free tất cả payload, metadata, scratch buffer và
LZO work memory. Discard block đầy đủ giải phóng payload ngay.

## Hạn chế

- Mọi I/O được tuần tự hóa nên throughput đa luồng không cao.
- LZO được chạy cho mọi lần ghi block, kể cả dữ liệu khó nén.
- Metadata được cấp theo logical capacity và không được nén.
- Không persistence, snapshot, encryption, quota hay bad-block recovery.
- `kmalloc` từng payload tạo allocator overhead và có thể phân mảnh RAM.
- Compression ratio procfs không tính overhead metadata/allocator.
- API block layer thay đổi nhanh; nhánh hiện tại nhắm Ubuntu 5.15+ và 6.x,
  không hỗ trợ kernel 5.x quá cũ dùng `make_request_fn`.

## Hướng phát triển

- Thay ordered workqueue bằng khóa theo block và nhiều worker;
- dùng xarray hoặc zsmalloc để giảm metadata/fragmentation;
- thêm lựa chọn LZ4/Zstd và tự chọn thuật toán theo dữ liệu;
- nhận biết zero block để không cấp payload;
- bổ sung tracepoint, debugfs histogram và benchmark fio;
- thêm KUnit test, fault injection và CI build trên nhiều kernel headers.
