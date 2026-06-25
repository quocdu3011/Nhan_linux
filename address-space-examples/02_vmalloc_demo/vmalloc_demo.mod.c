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
	{ 0xb1ad3f2f, "boot_cpu_data" },
	{ 0xbd03ed67, "vmalloc_base" },
	{ 0xd7a59a65, "vmalloc_noprof" },
	{ 0x6528d32c, "vmalloc_to_page" },
	{ 0xbd03ed67, "vmemmap_base" },
	{ 0xf1de9e85, "vfree" },
	{ 0xe8213e80, "_printk" },
	{ 0xd272d446, "__x86_return_thunk" },
	{ 0x814e12e5, "module_layout" },
};

static const u32 ____version_ext_crcs[]
__used __section("__version_ext_crcs") = {
	0xd272d446,
	0xb1ad3f2f,
	0xbd03ed67,
	0xd7a59a65,
	0x6528d32c,
	0xbd03ed67,
	0xf1de9e85,
	0xe8213e80,
	0xd272d446,
	0x814e12e5,
};
static const char ____version_ext_names[]
__used __section("__version_ext_names") =
	"__fentry__\0"
	"boot_cpu_data\0"
	"vmalloc_base\0"
	"vmalloc_noprof\0"
	"vmalloc_to_page\0"
	"vmemmap_base\0"
	"vfree\0"
	"_printk\0"
	"__x86_return_thunk\0"
	"module_layout\0"
;

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "0F21386B26A2740526F585E");
