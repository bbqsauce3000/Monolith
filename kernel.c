#include <stdint.h>

/* BEGIN_LINKER
ENTRY(kmain)
SECTIONS {
  . = 1M;
  .text : { *(.multiboot) *(.text*) }
  .bss  : { *(.bss*) *(COMMON) }
}
END_LINKER */

/* BEGIN_GRUBCFG
set timeout=0
set default=0
menuentry "Monolith" {
    multiboot /boot/kernel.elf
}
END_GRUBCFG */

__attribute__((section(".multiboot")))
const unsigned long mb_header[] = {
    0x1BADB002, 0x0, -(0x1BADB002)
};

static volatile uint16_t* const VGA = (uint16_t*)0xB8000;

void kmain(void) {
    const char* s = "this is literally one file";
    for (uint16_t i = 0; s[i]; i++)
        VGA[i] = 0x0F00 | s[i];

    for (;;) __asm__("hlt");
}
