#!/bin/sh
set -e

detect() {
    if command -v i686-elf-gcc >/dev/null; then
        CC=i686-elf-gcc; LD=i686-elf-ld; M=cross
    elif command -v clang >/dev/null; then
        CC=clang; LD=ld.lld; M=clang
    else
        CC=gcc; LD=ld; M=system
    fi
    echo "[monolith] $CC ($M)"
}

extract() {
    sed -n '/BEGIN_LINKER/,/END_LINKER/p' kernel.c | sed '1d;$d' > linker.ld
    sed -n '/BEGIN_GRUBCFG/,/END_GRUBCFG/p' kernel.c | sed '1d;$d' > grub.cfg
}

build() {
    detect
    case $M in
        cross)  $CC -ffreestanding -m32      -c kernel.c -o kernel.o ;;
        clang)  $CC -target i386-elf -ffreestanding -c kernel.c -o kernel.o ;;
        system) $CC -m32 -ffreestanding      -c kernel.c -o kernel.o ;;
    esac
    case $M in
        system) $LD -m elf_i386 -T linker.ld -o kernel.elf kernel.o ;;
        *)      $LD -T linker.ld -o kernel.elf kernel.o ;;
    esac
}

iso() {
    mkdir -p iso/boot/grub
    cp kernel.elf iso/boot/kernel.elf
    cp grub.cfg   iso/boot/grub/grub.cfg
    grub-mkrescue -o monolith.iso iso
}

run() { qemu-system-i386 -cdrom monolith.iso; }
clean() { rm -rf iso kernel.o kernel.elf linker.ld grub.cfg monolith.iso; }

case "$1" in
    build) extract; build ;;
    iso)   extract; build; iso ;;
    run)   extract; build; iso; run ;;
    clean) clean ;;
    *)     extract; build; iso ;;
esac
