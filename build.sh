#!/bin/sh
set -e

sed -n '/BEGIN_LINKER/,/END_LINKER/p' kernel.c | sed '1d;$d' > linker.ld
sed -n '/BEGIN_GRUBCFG/,/END_GRUBCFG/p' kernel.c | sed '1d;$d' > grub.cfg

mkdir -p build iso/boot/grub

gcc -m32 -fno-stack-protector -fno-pie -fno-pic \
    -finput-charset=utf-8 -fexec-charset=cp437 \
    -c kernel.c -o build/kernel.o

ld -m elf_i386 -T linker.ld -o build/kernel.elf build/kernel.o

cp build/kernel.elf iso/boot/kernel.elf
cp grub.cfg iso/boot/grub/grub.cfg

grub-mkrescue -o monolith.iso iso

if [ "$1" = "run" ]; then
    qemu-system-i386 -cdrom monolith.iso
fi
