savedcmd_kmap_demo.mod := printf '%s\n'   kmap_demo.o | awk '!x[$$0]++ { print("./"$$0) }' > kmap_demo.mod
