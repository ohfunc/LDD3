cmd_/home/bell/ldd3/src/hello/hello.ko := ld -r -m elf_x86_64  -z max-page-size=0x200000  --build-id  -T ./scripts/module-common.lds -o /home/bell/ldd3/src/hello/hello.ko /home/bell/ldd3/src/hello/hello.o /home/bell/ldd3/src/hello/hello.mod.o;  true