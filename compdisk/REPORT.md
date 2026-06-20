# Báo cáo kỹ thuật: CompDisk

## 1. Đặt vấn đề

Block device truyền thống ánh xạ sector logic xuống thiết bị lưu trữ vật lý.
Trong một số bài toán tạm thời như build cache, scratch filesystem hoặc nghiên
cứu hệ điều hành, tốc độ và việc giảm số byte RAM quan trọng hơn tính bền vững.
Đề tài xây dựng một block device nằm hoàn toàn trong RAM nhưng nén dữ liệu theo
block, có giao diện chuẩn để ext4 và công cụ user space sử dụng không cần sửa.

## 2. Mục tiêu

- Tạo `/dev/compdisk` dung lượng logic mặc định 64 MiB, cấu hình khi load;
- tiếp nhận BIO read/write từ block layer và chạy được ext4;
- nén độc lập mỗi block 4096 byte bằng LZO;
- lưu raw nếu nén không đạt ngưỡng tiết kiệm;
- cung cấp số liệu sử dụng/nén qua `/proc/compdisk_stats`;
- không truy cập SSD/HDD và giải phóng toàn bộ RAM khi unload;
- build được trên kernel Ubuntu hiện đại 5.15+ và 6.x.

## 3. Cơ sở lý thuyết

### 3.1 Kernel module

Kernel module là object ELF có thể nạp động bằng `insmod`. Hàm `module_init`
đăng ký tài nguyên; `module_exit` tháo đăng ký và giải phóng chúng. Code chạy ở
kernel space nên lỗi con trỏ, race hoặc giải phóng sai có thể làm treo hệ thống.
Module dùng GPL, tham số read-only và Kbuild với headers của kernel đang chạy.

### 3.2 Block device driver

`gendisk` biểu diễn đĩa, chứa tên, capacity, major/minor và bảng
`block_device_operations`. `register_blkdev()` cấp major; `device_add_disk()`
công bố đĩa để udev tạo `/dev/compdisk`. Capacity được khai báo theo sector 512
byte, còn logical/physical block size được đặt là 4096 byte.

### 3.3 BIO và request

BIO mô tả một phép I/O bằng sector bắt đầu, độ dài, operation và danh sách
`bio_vec` trỏ tới page. Driver BIO-based cài callback `submit_bio`. Callback chỉ
cấp work item bằng `GFP_ATOMIC` rồi queue; worker mới duyệt segment, map page,
nén/cấp phát và gọi `bio_endio`. Cách này tránh sleep trong đường submit.

CompDisk xử lý `REQ_OP_READ`, `WRITE`, `FLUSH`, `DISCARD` và `WRITE_ZEROES`.
Flush không cần ghi xuống media vì dữ liệu đã ở RAM; ordered workqueue bảo đảm
các write được xử lý trước flush theo thứ tự queue.

### 3.4 Nén trong kernel

Kernel cung cấp LZO qua `<linux/lzo.h>`. Ghi dùng `lzo1x_1_compress` với vùng
work memory `LZO1X_MEM_COMPRESS`; đọc dùng `lzo1x_decompress_safe`. Giải nén an
toàn phải trả `LZO_E_OK` và đúng 4096 byte, nếu không block được coi là hỏng.
Nén từng block giới hạn phạm vi giải nén và cho phép truy cập ngẫu nhiên.

### 3.5 RAM-backed storage

Không có backing file hay lower block device. Mảng metadata nằm trong RAM và
mỗi payload được `kmalloc` khi ghi lần đầu. Block chưa có payload có nội dung
logic toàn zero. Khi discard block đầy đủ, payload được giải phóng.

## 4. Thiết kế hệ thống

```text
User space / ext4
        │
        ▼
    Block layer
        │ submit_bio
        ▼
Ordered workqueue ── mutex ── metadata + statistics
        │
        ├─ read: zero / memcpy raw / LZO decompress
        └─ write: partial-block merge / LZO / raw-or-compressed
                                      │
                                      ▼
                                payload trong RAM
```

Ordered workqueue được chọn để bảo toàn thứ tự và đơn giản hóa đồng thời. Mutex
còn đồng bộ procfs với worker. Đổi lại, mọi BIO chạy nối tiếp.

Init thực hiện: kiểm tra parameter → cấp device/metadata/scratch → tạo
workqueue → đăng ký major → cấp/cấu hình gendisk → tạo procfs → add disk. Các
nhánh lỗi rollback theo thứ tự ngược. Exit thực hiện chiều ngược và destroy
workqueue để drain BIO còn lại trước khi free storage.

## 5. Cấu trúc dữ liệu

Metadata mỗi block gồm:

```c
struct compdisk_block {
    void *data;
    u32 size;
    bool compressed;
    bool compression_skipped;
};
```

`data == NULL` nghĩa là block chưa ghi và đọc ra zero. `size` là kích thước
payload thực. Cờ `compression_skipped` phân biệt raw vì không đủ saving với raw
do lỗi LZO, giúp thống kê trạng thái hiện tại chính xác khi overwrite/discard.

Device còn chứa capacity, `gendisk`, workqueue, proc entry, mutex, bốn vùng
scratch/work memory và các counter 64 bit. Scratch dùng chung là an toàn vì chỉ
một ordered worker hoạt động và procfs không sửa scratch.

## 6. Thuật toán ghi

1. Tính byte position từ `bi_sector << 9`; kiểm tra phép cộng bằng dạng
   `position > capacity || length > capacity - position` để tránh overflow.
2. Duyệt `bio_vec`, map page cục bộ và copy tối đa 4096 byte vào scratch; không
   sleep khi page còn map.
3. Chia dữ liệu theo biên block. Với ghi không phủ cả block, đọc block cũ vào
   raw scratch để thực hiện read-modify-write.
4. Gọi LZO cho đúng 4096 byte.
5. Chọn compressed khi payload nhỏ hơn raw và saving phần trăm đạt
   `min_saving_percent`; nếu không, chọn raw.
6. Cấp payload mới. Chỉ sau khi thành công mới thay metadata, cập nhật counter
   và free payload cũ.
7. Nếu có lỗi, đặt `bi_status` thích hợp và luôn gọi `bio_endio`.

## 7. Thuật toán đọc

1. Kiểm tra position/length giống luồng ghi.
2. Với mỗi block: sinh zero nếu chưa ghi; copy nếu raw; nếu compressed thì gọi
   `lzo1x_decompress_safe` và xác minh output đúng 4096 byte.
3. Copy đúng phần offset/length được yêu cầu sang page của BIO.
4. Hoàn thành BIO. Metadata sai hoặc dữ liệu nén hỏng trả lỗi I/O.

## 8. Adaptive compression và thống kê

Ngưỡng mặc định là tiết kiệm ít nhất 10%. Với raw size `R = 4096` và compressed
size `C`, block chỉ lưu compressed khi `C < R` và:

```text
(R - C) × 100 >= R × min_saving_percent
```

Procfs báo dung lượng logic, tổng/đã ghi, số block compressed/raw/bị bỏ nén,
payload physical, ratio, phần trăm tiết kiệm và số BIO. `Written blocks`, các
nhóm block và byte physical là gauge trạng thái hiện tại; request và lỗi LZO là
counter tích lũy. Physical bytes không bao gồm overhead metadata/allocator.

## 9. Kết quả kiểm thử

Kiểm tra tĩnh/build đã thực hiện:

- Kbuild dùng headers `/lib/modules/6.17.0-35-generic/build`;
- `make W=1` tạo `compdisk.ko` thành công, không có warning từ source;
- `modinfo` xác nhận module, license, vermagic và hai parameter;
- `bash -n scripts/*.sh` thành công.

`scripts/test.sh` đã được chạy thành công với quyền root: format ext4, mount,
ghi 10 MiB zero và 10 MiB random, sync, lấy checksum, unmount/mount lại, so
sánh zero và checksum, rồi xác nhận thống kê có cả compressed và raw block.
Kết quả cuối cùng là `PASS`.

Kết quả đo trên Ubuntu kernel 6.17.0-35-generic, đĩa logic 64 MiB và ngưỡng
tiết kiệm 10%:

```text
Written blocks: 6178
Compressed blocks: 3618
Raw blocks: 2560
Blocks skipped (insufficient saving): 2560
Compression failures: 0
Logical used: 25305088 bytes
Physical used: 10647321 bytes
Compression ratio: 2.37
Memory saved: 57.9%
Read requests: 290
Write requests: 58
```

Zero tạo phần lớn block compressed, random tạo block raw và không có lỗi LZO.
Số block logic đã dùng lớn hơn đúng 20 MiB dữ liệu file do còn metadata và
journal của ext4.

## 10. Đánh giá

Ưu điểm:

- giao diện block chuẩn, dùng filesystem/công cụ hiện hữu;
- random access theo block, không cần giải nén toàn đĩa;
- adaptive compression tránh payload nén lớn hơn hoặc gần bằng raw;
- block chưa ghi không tốn payload, discard thu hồi RAM;
- thay payload theo kiểu allocate-before-swap và rollback init đầy đủ.

Nhược điểm:

- tuần tự hóa toàn bộ I/O giới hạn SMP throughput;
- thử LZO mỗi lần ghi tiêu tốn CPU với dữ liệu entropy cao;
- `kmalloc` từng block có overhead và fragmentation;
- metadata tỷ lệ tuyến tính với logical capacity;
- không persistence và không có redundancy;
- số liệu physical chưa tính toàn bộ footprint kernel.

## 11. Kết luận

CompDisk hiện thực đầy đủ đường dữ liệu của một RAM-backed compressed block
device: đăng ký gendisk, xử lý BIO có kiểm tra biên, nén/giải nén LZO theo block,
adaptive raw fallback, discard, procfs và teardown không để lại payload. Module
phù hợp cho mục đích học tập về block layer, quản lý bộ nhớ và nén trong kernel.
Hướng tiếp theo có giá trị nhất là khóa theo block với nhiều worker, allocator
chuyên cho object nén như zsmalloc và benchmark/fault-injection tự động.
