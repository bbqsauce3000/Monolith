#include <stdint.h>

__attribute__((section(".multiboot"), aligned(4), used))
const uint32_t mb_header[] = {
    0x1BADB002,
    0x00000000,
    -(0x1BADB002 + 0x00000000)
};

/* BEGIN_LINKER
ENTRY(kmain)
SECTIONS {
  . = 1M;
  .text : { KEEP(*(.multiboot)) *(.text*) }
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


// GDT structures
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct gdt_entry gdt[3];
static struct gdt_ptr   gdtp;

static uint16_t seg_cs, seg_ds;

// Set a GDT entry
static void gdt_set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].base_low  = base & 0xFFFF;
    gdt[i].base_mid  = (base >> 16) & 0xFF;
    gdt[i].access    = access;
    gdt[i].gran      = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].base_high = (base >> 24) & 0xFF;
}

// Load the GDT and enter protected mode cleanly
static void gdt_init(void) {
    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base  = (uint32_t)&gdt;

    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xCF);
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xCF);

    __asm__ volatile (
        "lgdt (%0)\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "jmp $0x08, $.1\n"
        ".1:\n"
        :
        : "r" (&gdtp)
        : "ax"
    );

    __asm__ volatile ("mov %%cs, %0" : "=r"(seg_cs));
    __asm__ volatile ("mov %%ds, %0" : "=r"(seg_ds));
}

// VGA text buffer
static volatile uint16_t* const VGA = (uint16_t*)0xB8000;

// Print a 16‑bit hex value
static void put_hex(uint16_t v, int* p) {
    const char* h = "0123456789ABCDEF";
    VGA[(*p)++] = 0x0F00 | h[(v >> 12) & 0xF];
    VGA[(*p)++] = 0x0F00 | h[(v >> 8) & 0xF];
    VGA[(*p)++] = 0x0F00 | h[(v >> 4) & 0xF];
    VGA[(*p)++] = 0x0F00 | h[v & 0xF];
}

// Scrolling + full VGA palette cycling effect
static void pixel_demo_textmode(void) {
    const uint16_t block = 0xDB;
    static int t = 0;
    static int slow = 0;

    if (++slow >= 4) {
        t++;
        slow = 0;
    }

    for (int y = 6; y < 20; y++) {
        for (int x = 0; x < 80; x++) {
            int sx = (x + t) & 0x7F;
            uint8_t color = ((x + t/2) >> 2) & 0x0F;
            if ((x + t) & 8)
             color = (color + 1) & 0x0F;

            VGA[y * 80 + x] = (color << 8) | block;
        }
    }
}


// Kernel entry
void kmain(void) {
    gdt_init();

    // Clear screen
    for (int i = 0; i < 80 * 25; i++)
        VGA[i] = 0x0F00 | ' ';

    int p = 0;

    const char* a = "This is all in one file";
    for (int i = 0; a[i]; i++) VGA[p++] = 0x0F00 | a[i];

    p = 80;
    const char* b = "This is possible because it has the asm, GRUB, and .ld code all in the C file.";
    for (int i = 0; b[i]; i++) VGA[p++] = 0x0F00 | b[i];

    p = 160;
    const char* c = "Text is just bytes, therefore you can encode anything within those bytes.";
    for (int i = 0; c[i]; i++) VGA[p++] = 0x0F00 | c[i];

    p = 240;
    const char* d = "GDT PROOF: CS=";
    for (int i = 0; d[i]; i++) VGA[p++] = 0x0F00 | d[i];

    put_hex(seg_cs, &p);

    const char* e = " DS=";
    for (int i = 0; e[i]; i++) VGA[p++] = 0x0F00 | e[i];

    put_hex(seg_ds, &p);

    // Animation loop
    for (;;) {
        pixel_demo_textmode();
        for (volatile int i = 0; i < 2000000; i++);  // delay
    }
}
