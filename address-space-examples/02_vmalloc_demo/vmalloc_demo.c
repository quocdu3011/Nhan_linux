/*
 * Ví dụ: vmalloc/vfree - cấp phát bộ nhớ không liên tục về mặt vật lý
 * nhưng liên tục về mặt virtual trong vùng VMALLOC.
 * Nguồn: https://linux-kernel-labs.github.io/refs/heads/master/lectures/address-space.html
 */
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("vmalloc Demo");

static int __init vmalloc_demo_init(void)
{
    void *buf;
    struct page *pg;
    int i;

    pr_info("=== vmalloc Demo ===\n");
    pr_info("VMALLOC area: 0x%lx - 0x%lx\n", VMALLOC_START, VMALLOC_END);

    /* Cấp phát 4 pages không liên tục vật lý */
    buf = vmalloc(PAGE_SIZE * 4);
    if (!buf)
        return -ENOMEM;

    pr_info("vmalloc(%lu) -> vaddr=%p\n", PAGE_SIZE * 4, buf);

    /* Mỗi virtual page có thể nằm ở physical page khác nhau */
    for (i = 0; i < 4; i++) {
        pg = vmalloc_to_page(buf + i * PAGE_SIZE);
        if (pg)
            pr_info("  page[%d]: pfn=%lu phys=0x%llx\n",
                    i, page_to_pfn(pg),
                    (unsigned long long)page_to_phys(pg));
    }

    vfree(buf);
    pr_info("vfree: done\n");
    return 0;
}

static void __exit vmalloc_demo_exit(void)
{
    pr_info("=== vmalloc Demo unloaded ===\n");
}

module_init(vmalloc_demo_init);
module_exit(vmalloc_demo_exit);
