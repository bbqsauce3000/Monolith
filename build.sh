#!/bin/sh
set -e

# Extract embedded sections from kernel.c
sed -n '/BEGIN_LINKER/,/END_LINKER/p' kernel.c | sed '1d;$d' > linker.ld
sed -n '/BEGIN_GRUBCFG/,/END_GRUBCFG/p' kernel.c | sed '1d;$d' > grub.cfg
sed -n '/BEGIN_INITRD/,/END_INITRD/p' kernel.c \
    | tail -n +2 | head -n -1 \
    | tr -d '\r' \
    > initrd.txt

echo "Building initrd.img"
rm -f initrd.img

while IFS=: read -r name content; do
    # Write: name\0data\0
    printf "%s" "$name" >> initrd.img
    printf "\0" >> initrd.img
    printf "%s" "$content" >> initrd.img
    printf "\0" >> initrd.img
done < initrd.txt

mkdir -p build iso/boot/grub

gcc -m32 -fno-stack-protector -fno-pie -fno-pic \
    -finput-charset=utf-8 -fexec-charset=cp437 \
    -c kernel.c -o build/kernel.o

ld -m elf_i386 -T linker.ld -o build/kernel.elf build/kernel.o

cp build/kernel.elf iso/boot/kernel.elf
cp grub.cfg iso/boot/grub/grub.cfg
cp initrd.img iso/boot/

grub-mkrescue -o monolith.iso iso

if [ "$1" = "run" ]; then
    qemu-system-i386 -cdrom monolith.iso
fi
