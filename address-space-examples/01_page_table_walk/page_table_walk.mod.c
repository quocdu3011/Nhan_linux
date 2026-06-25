#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xd272d446, "__fentry__" },
	{ 0xd94efd11, "const_current_task" },
	{ 0xdbd50071, "get_task_mm" },
	{ 0x89a2c348, "__tracepoint_mmap_lock_start_locking" },
	{ 0xa59da3c0, "down_read" },
	{ 0x89a2c348, "__tracepoint_mmap_lock_acquire_returned" },
	{ 0xf296206e, "pgdir_shift" },
	{ 0xb1ad3f2f, "boot_cpu_data" },
	{ 0x89a2c348, "__tracepoint_mmap_lock_released" },
	{ 0xa59da3c0, "up_read" },
	{ 0x40ae3ae0, "mmput" },
	{ 0x095159b2, "physical_mask" },
	{ 0x1bdf2bc8, "sme_me_mask" },
	{ 0xf296206e, "ptrs_per_p4d" },
	{ 0x2dd980bc, "pv_ops" },
	{ 0xd272d446, "BUG_func" },
	{ 0xbd03ed67, "page_offset_base" },
	{ 0x03667368, "__mmap_lock_do_trace_acquire_returned" },
	{ 0x59922020, "__mmap_lock_do_trace_start_locking" },
	{ 0x59922020, "__mmap_lock_do_trace_released" },
	{ 0x82fd7238, "__ubsan_handle_shift_out_of_bounds" },
	{ 0xe8213e80, "_printk" },
	{ 0xd272d446, "__x86_return_thunk" },
	{ 0x814e12e5, "module_layout" },
};

static const u32 ____version_ext_crcs[]
__used __section("__version_ext_crcs") = {
	0xd272d446,
	0xd94efd11,
	0xdbd50071,
	0x89a2c348,
	0xa59da3c0,
	0x89a2c348,
	0xf296206e,
	0xb1ad3f2f,
	0x89a2c348,
	0xa59da3c0,
	0x40ae3ae0,
	0x095159b2,
	0x1bdf2bc8,
	0xf296206e,
	0x2dd980bc,
	0xd272d446,
	0xbd03ed67,
	0x03667368,
	0x59922020,
	0x59922020,
	0x82fd7238,
	0xe8213e80,
	0xd272d446,
	0x814e12e5,
};
static const char ____version_ext_names[]
__used __section("__version_ext_names") =
	"__fentry__\0"
	"const_current_task\0"
	"get_task_mm\0"
	"__tracepoint_mmap_lock_start_locking\0"
	"down_read\0"
	"__tracepoint_mmap_lock_acquire_returned\0"
	"pgdir_shift\0"
	"boot_cpu_data\0"
	"__tracepoint_mmap_lock_released\0"
	"up_read\0"
	"mmput\0"
	"physical_mask\0"
	"sme_me_mask\0"
	"ptrs_per_p4d\0"
	"pv_ops\0"
	"BUG_func\0"
	"page_offset_base\0"
	"__mmap_lock_do_trace_acquire_returned\0"
	"__mmap_lock_do_trace_start_locking\0"
	"__mmap_lock_do_trace_released\0"
	"__ubsan_handle_shift_out_of_bounds\0"
	"_printk\0"
	"__x86_return_thunk\0"
	"module_layout\0"
;

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "F5BB85D0004BC75A182B0C6");
