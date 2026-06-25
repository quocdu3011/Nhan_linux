savedcmd_vmalloc_demo.mod := printf '%s\n'   vmalloc_demo.o | awk '!x[$$0]++ { print("./"$$0) }' > vmalloc_demo.mod
