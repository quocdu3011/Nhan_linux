/*
 * Ví dụ: Walk page table để dịch virtual address -> physical address
 * Nguồn: https://linux-kernel-labs.github.io/refs/heads/master/lectures/address-space.html
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Page Table Walk Demo");

static void walk_page_table(struct mm_struct *mm, unsigned long vaddr)
{
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    phys_addr_t phys;

    pgd = pgd_offset(mm, vaddr);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        pr_info("  pgd: not present\n");
        return;
    }

    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        pr_info("  p4d: not present\n");
        return;
    }

    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud) || pud_bad(*pud)) {
        pr_info("  pud: not present\n");
        return;
    }

    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) {
        pr_info("  pmd: not present\n");
        return;
    }

    pte = pte_offset_kernel(pmd, vaddr);
    if (!pte) {
        pr_info("  pte: map failed\n");
        return;
    }

    if (pte_present(*pte)) {
        phys = ((phys_addr_t)pte_pfn(*pte) << PAGE_SHIFT) | (vaddr & ~PAGE_MASK);
        pr_info("  vaddr=0x%lx -> phys=0x%llx (pfn=%lu)\n",
                vaddr, (unsigned long long)phys, pte_pfn(*pte));
    } else {
        pr_info("  pte: page not present in memory\n");
    }
}

static int __init pt_walk_init(void)
{
    struct mm_struct *mm;
    unsigned long vaddr;

    mm = get_task_mm(current);
    if (!mm) {
        pr_info("current task has no user mm\n");
        return -EINVAL;
    }

    /* Walk một địa chỉ user-space của process gọi insmod. */
    vaddr = mm->start_code;

    pr_info("=== Page Table Walk Demo ===\n");
    pr_info("Walking page table for current process addr: 0x%lx\n", vaddr);

    mmap_read_lock(mm);
    walk_page_table(mm, vaddr);
    mmap_read_unlock(mm);
    mmput(mm);

    return 0;
}

static void __exit pt_walk_exit(void)
{
    pr_info("=== Page Table Walk Demo unloaded ===\n");
}

module_init(pt_walk_init);
module_exit(pt_walk_exit);
