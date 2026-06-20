# Kiến trúc CompDisk

## Thành phần

```text
Ứng dụng / ext4
       │ BIO read/write/flush/discard
       ▼
Linux block layer
       │ compdisk_submit_bio()
       ▼
Ordered workqueue ──► kiểm tra phạm vi và tuần tự hóa I/O
       │
       ├── read  ──► metadata[block] ──► LZO decompress/raw/zero
       ├── write ──► read-modify-write ──► LZO compress ──► raw/compressed
       └── discard ──► giải phóng payload của block đầy đủ
                              │
                              ▼
                       RAM (kmalloc/kvcalloc)
```

`gendisk` đại diện `/dev/compdisk`. Mảng `compdisk_block` chỉ chứa metadata; payload chỉ được cấp phát khi block được ghi. Mỗi payload dài tối đa 4096 byte.

## Đồng thời và ngữ cảnh thực thi

Callback `submit_bio` không nén và không chờ mutex. Nó cấp một work item bằng `GFP_NOIO`, đưa BIO vào ordered workqueue rồi trả về. Worker có thể sleep, dùng mutex và bốn scratch buffer dùng chung. Ordered workqueue đảm bảo chỉ một worker sửa metadata/scratch buffer tại một thời điểm và giữ thứ tự flush.

Procfs cũng giữ mutex khi lấy snapshot, nên các bộ đếm không bị đọc giữa lúc thay một block.

`Blocks skipped (insufficient saving)` là số block raw hiện tại do không đạt
ngưỡng, không phải bộ đếm tích lũy số lần ghi. Khi overwrite hoặc discard,
driver giảm bộ đếm trạng thái cũ trước khi tăng trạng thái mới.

## Luồng ghi

```text
BIO WRITE
  → kiểm tra sector + length không vượt capacity
  → chép segment từ page sang io_scratch
  → với từng chunk 4 KiB:
      → nếu ghi một phần: giải nén block cũ hoặc khởi tạo zero
      → cập nhật byte mới trong raw_scratch
      → thử LZO
      → nếu tiết kiệm > min_saving_percent: kmemdup compressed
      → ngược lại: kmemdup 4096 byte raw
      → hoán đổi con trỏ, cập nhật thống kê, free payload cũ
  → bio_endio
```

Payload cũ chỉ được giải phóng sau khi payload mới đã cấp phát thành công. Vì vậy lỗi thiếu RAM không phá dữ liệu block đang có.

## Luồng đọc

```text
BIO READ
  → kiểm tra phạm vi
  → với từng chunk:
      → data == NULL: sinh 4096 byte zero
      → compressed: lzo1x_decompress_safe và kiểm tra output == 4096
      → raw: memcpy 4096 byte
      → copy phần BIO yêu cầu vào page
  → bio_endio
```

## Vòng đời

Khi init: kiểm tra parameter → cấp metadata/scratch → tạo workqueue → đăng ký major → cấp `gendisk` → tạo procfs → `device_add_disk`. Mọi nhánh lỗi tháo ngược các tài nguyên đã tạo.

Khi exit: xóa procfs → `del_gendisk` → flush/destroy workqueue → `put_disk` → unregister major → giải phóng mọi payload, metadata và scratch buffer.
