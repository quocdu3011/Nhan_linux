/*
 * Ví dụ: TLB flush - invalidate TLB entry bằng inline assembly x86
 * Nguồn: https://linux-kernel-labs.github.io/refs/heads/master/lectures/address-space.html
 *
 * Tài liệu mô tả 2 cách:
 *   1. invlpg <addr>  - flush một entry
 *   2. reload CR3     - flush toàn bộ TLB
 */
#include <linux/module.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TLB Flush Demo");

/* Flush một TLB entry cho địa chỉ addr */
static void demo_flush_tlb_single(unsigned long addr)
{
    asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

/* Flush toàn bộ TLB bằng cách reload CR3 */
static void demo_flush_tlb_full(void)
{
    unsigned long cr3;
    asm volatile(
        "mov %%cr3, %0\n\t"
        "mov %0, %%cr3"
        : "=r"(cr3) : : "memory"
    );
}

static int __init tlb_init(void)
{
    unsigned long addr = (unsigned long)tlb_init;

    pr_info("=== TLB Flush Demo ===\n");

    pr_info("invlpg: flush TLB entry for addr=0x%lx\n", addr);
    demo_flush_tlb_single(addr);

    pr_info("reload CR3: flush full TLB\n");
    demo_flush_tlb_full();

    pr_info("TLB flush complete\n");
    return 0;
}

static void __exit tlb_exit(void)
{
    pr_info("=== TLB Flush Demo unloaded ===\n");
}

module_init(tlb_init);
module_exit(tlb_exit);
