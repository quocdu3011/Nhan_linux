/*
 * Ví dụ: kmap_local_page - temporary mapping cho high memory pages
 * (kmap_local_page là phiên bản hiện đại của kmap_atomic trong tài liệu)
 * Nguồn: https://linux-kernel-labs.github.io/refs/heads/master/lectures/address-space.html
 */
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/gfp.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("kmap Demo");

static int __init kmap_demo_init(void)
{
    struct page *page;
    void *vaddr;
    u8 *data;

    pr_info("=== kmap Demo ===\n");

    /* Cấp phát một high memory page (nếu có) */
    page = alloc_page(GFP_KERNEL | __GFP_HIGHMEM);
    if (!page)
        return -ENOMEM;

    pr_info("allocated page: pfn=%lu is_highmem=%d\n",
            page_to_pfn(page), PageHighMem(page));

    /* kmap_local_page: tạo temporary virtual mapping */
    vaddr = kmap_local_page(page);
    pr_info("kmap_local_page -> vaddr=%p\n", vaddr);

    /* Ghi/đọc dữ liệu vào page qua virtual address */
    memset(vaddr, 0xAB, PAGE_SIZE);
    data = (u8 *)vaddr;
    pr_info("wrote 0xAB, read back: 0x%02x\n", data[0]);

    /* Giải phóng temporary mapping */
    kunmap_local(vaddr);
    pr_info("kunmap_local: done\n");

    __free_page(page);
    return 0;
}

static void __exit kmap_demo_exit(void)
{
    pr_info("=== kmap Demo unloaded ===\n");
}

module_init(kmap_demo_init);
module_exit(kmap_demo_exit);
