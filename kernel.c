#include <stdint.h>
#include <stddef.h>

static uint32_t lower = 0;
static uint32_t upper = 0;

__attribute__((section(".multiboot"), aligned(4), used))
const uint32_t mb_header[] = {
    0x1BADB002,            // magic
    0x00000003,            // flags: request mem info + mmap
    -(0x1BADB002 + 0x00000003)
};


// Multiboot memory map entry
typedef struct {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed)) multiboot_mmap_entry_t;

// Full Multiboot info struct (only fields I care about)
typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} __attribute__((packed)) multiboot_info_t;

typedef struct {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;
} __attribute__((packed)) multiboot_module_t;

static const char* monolith_banner =
"╔╦╗╔═╗╔╗╔╔═╗╦  ╦╔╦╗╦ ╦\n"
"║║║║ ║║║║║ ║║  ║ ║ ╠═╣\n"
"╩ ╩╚═╝╝╚╝╚═╝╩═╝╩ ╩ ╩ ╩\n";

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
    module  /boot/initrd.img
}
END_GRUBCFG */

/* BEGIN_INITRD
hello.txt:Hello from Monolith!
motd.txt:Welcome to the one-file OS.
END_INITRD */

// GDT structures
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
} __attribute__((packed));

// GDT pointer
struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

// IDT entry structure
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

// IDT pointer
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

typedef struct {
    const char* name;
    const char* data;
    uint32_t size;
} initrd_file_t;

#define MAX_INITRD_FILES 32
static initrd_file_t initrd_files[MAX_INITRD_FILES];
static int initrd_file_count = 0;

static void initrd_parse(uint32_t start, uint32_t end) {
    const char* p = (const char*)start;
    initrd_file_count = 0;

    while ((uint32_t)p < end && initrd_file_count < MAX_INITRD_FILES) {
        // filename (NUL-terminated)
        const char* name = p;
        while ((uint32_t)p < end && *p != '\0') p++;
        if ((uint32_t)p >= end) break;
        p++; // skip NUL

        // data (NUL-terminated)
        const char* data = p;
        while ((uint32_t)p < end && *p != '\0') p++;
        if ((uint32_t)p >= end) break;
        uint32_t size = p - data;
        p++; // skip NUL

        initrd_files[initrd_file_count].name = name;
        initrd_files[initrd_file_count].data = data;
        initrd_files[initrd_file_count].size = size;
        initrd_file_count++;
    }
}

// Global descriptor table and pointers
static struct gdt_entry gdt[3];
static struct gdt_ptr   gdtp;
static struct idt_entry idt[256];
static struct idt_ptr   idtp;

// Saved segment registers
static uint16_t seg_cs, seg_ds;

// VGA text buffer base
static volatile uint16_t* const VGA = (uint16_t*)0xB8000;

// I/O port read
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "dN"(port));
    return v;
}

// I/O port write
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile ("outb %0, %1" :: "a"(v), "dN"(port));
}

// Set a GDT entry
static void gdt_set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].base_low  = base & 0xFFFF;
    gdt[i].base_mid  = (base >> 16) & 0xFF;
    gdt[i].access    = access;
    gdt[i].gran      = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].base_high = (base >> 24) & 0xFF;
}

// Load the GDT and update segment registers
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

// Set an IDT entry
static void idt_set_entry(int i, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[i].offset_low  = base & 0xFFFF;
    idt[i].selector    = sel;
    idt[i].zero        = 0;
    idt[i].type_attr   = flags;
    idt[i].offset_high = (base >> 16) & 0xFFFF;
}

// Write a character to VGA text buffer at position pos
static void putch(int pos, char c, uint8_t attr) {
    VGA[pos] = ((uint16_t)attr << 8) | (uint8_t)c;
}

// Write a NUL-terminated string to VGA starting at pos
static void putstr(int pos, const char* s, uint8_t attr) {
    for (int i = 0; s[i]; i++) putch(pos++, s[i], attr);
}

// Current cursor position
static int cursor = 0;

// Clear the screen and reset cursor
static void clear_screen(uint8_t attr) {
    for (int i = 0; i < 80 * 25; i++)
        VGA[i] = ((uint16_t)attr << 8) | ' ';
    cursor = 0;
}

// Scroll the screen up by one line
static void scroll_up(void) {
    for (int y = 1; y < 25; y++)
        for (int x = 0; x < 80; x++)
            VGA[(y - 1) * 80 + x] = VGA[y * 80 + x];

    for (int x = 0; x < 80; x++)
        VGA[24 * 80 + x] = 0x0F00 | ' ';

    cursor -= 80;
    if (cursor < 0) cursor = 0;
}

// Move to the next line, scrolling if needed
static void newline(void) {
    cursor = (cursor / 80 + 1) * 80;
    if (cursor >= 80 * 25) {
        scroll_up();
        cursor = 24 * 80;
    }
}

// Print the shell prompt
static void print_prompt(void) {
    putstr(cursor, "$ ", 0x0F);
    cursor += 2;
}

// Banner
static void print_banner(void) {
    for (int i = 0; monolith_banner[i]; i++) {
        char c = monolith_banner[i];
        if (c == '\n') {
            newline();
        } else {
            putch(cursor++, c, 0x0F);
        }
    }
    newline();
}

// Draw the demo screen
static void draw_demo(void) {
    clear_screen(0x0F);

    int p = 0;
    const char* a = "This is all in one file";
    for (int i = 0; a[i]; i++) putch(p++, a[i], 0x0F);

    p = 80;
    const char* b = "This is possible because it has the asm, GRUB, and .ld code all in the C file.";
    for (int i = 0; b[i]; i++) putch(p++, b[i], 0x0F);

    p = 160;
    const char* c = "Text is just bytes, therefore you can encode anything within those bytes.";
    for (int i = 0; c[i]; i++) putch(p++, c[i], 0x0F);

    p = 240;
    const char* d = "GDT PROOF: CS=";
    for (int i = 0; d[i]; i++) putch(p++, d[i], 0x0F);

    const char* h = "0123456789ABCDEF";
    putch(p++, h[(seg_cs >> 12) & 0xF], 0x0F);
    putch(p++, h[(seg_cs >> 8) & 0xF], 0x0F);
    putch(p++, h[(seg_cs >> 4) & 0xF], 0x0F);
    putch(p++, h[seg_cs & 0xF], 0x0F);

    const char* e = " DS=";
    for (int i = 0; e[i]; i++) putch(p++, e[i], 0x0F);

    putch(p++, h[(seg_ds >> 12) & 0xF], 0x0F);
    putch(p++, h[(seg_ds >> 8) & 0xF], 0x0F);
    putch(p++, h[(seg_ds >> 4) & 0xF], 0x0F);
    putch(p++, h[seg_ds & 0xF], 0x0F);

    cursor = ((p / 80) + 1) * 80;
}

// Simple color animation demo
static void color_demo(void) {
    for (int frame = 0; frame < 90; frame++) {
        static int t = 0;
        t++;

        for (int y = 6; y < 20; y++) {
            for (int x = 0; x < 80; x++) {
                uint8_t color = ((x + t/2) >> 2) & 0x0F;
                if ((x + t) & 8) color = (color + 1) & 0x0F;
                VGA[y * 80 + x] = (color << 8) | 0xDB;
            }
        }

        for (volatile int i = 0; i < 2000000; i++);
    }

    draw_demo();
}

#define SHELL_BUF_SIZE 128
static char shell_buf[SHELL_BUF_SIZE];
static int shell_len = 0;

// Normal keymap
static const char keymap_normal[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
   '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\','z',
    'x','c','v','b','n','m',',','.','/',0,0,0,' ',0,0,
};

// Shifted keymap
static const char keymap_shift[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+', '\b',
   '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|','Z',
    'X','C','V','B','N','M','<','>','?',0,0,0,' ',0,0,
};

static int shift_down = 0;
static int caps_lock = 0;

// PIT timing counter
static volatile unsigned long pit_ticks = 0;

// Initialize PIT
static void pit_init(void) {
    uint16_t divisor = 1193;

    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, divisor >> 8);
}

// PIT interrupt handler
void pit_handler_c(void) {
    pit_ticks++;
    outb(0x20, 0x20);
}

// PIT IRQ stub
__attribute__((naked))
void pit_stub(void) {
    __asm__ volatile (
        "pusha\n"
        "push %ds\n"
        "push %es\n"
        "push %fs\n"
        "push %gs\n"
        "mov $0x10, %ax\n"
        "mov %ax, %ds\n"
        "mov %ax, %es\n"
        "call pit_handler_c\n"
        "pop %gs\n"
        "pop %fs\n"
        "pop %es\n"
        "pop %ds\n"
        "popa\n"
        "iret\n"
    );
}

// Typing test state
static int typing_mode = 0;
static unsigned long typing_start = 0;
static const char* typing_text = "the quick brown fox jumps over the lazy dog";

// Compare two strings
static int str_eq(const char* a, const char* b) {
    for (;;) {
        if (*a != *b) return 0;
        if (!*a) return 1;
        a++; b++;
    }
}

// Check prefix
static int str_startswith(const char* s, const char* pre) {
    while (*pre) {
        if (*s++ != *pre++) return 0;
    }
    return 1;
}

// Print unsigned integer
static void print_uint(unsigned int v) {
    char buf[16];
    int i = 0;
    if (v == 0) {
        putch(cursor++, '0', 0x0F);
        return;
    }
    while (v > 0 && i < 16) {
        buf[i++] = '0' + (v % 10);
        v /= 10;
    }
    for (int j = i - 1; j >= 0; j--) {
        putch(cursor++, buf[j], 0x0F);
    }
}

// For the ram command, and the ram command only.
static void print_decimal(uint32_t value, uint32_t scale) {
    uint32_t integer = value / scale;
    uint32_t frac = ((value % scale) * 1000) / scale;  // 3 decimal places

    print_uint(integer);
    putch(cursor++, '.', 0x0F);

    // Always print 3 digits, including leading zeros
    putch(cursor++, '0' + (frac / 100), 0x0F);
    putch(cursor++, '0' + ((frac / 10) % 10), 0x0F);
    putch(cursor++, '0' + (frac % 10), 0x0F);
}

// Handle a shell command
static void handle_command(const char* cmd) {
    if (typing_mode) {
        typing_mode = 0;
        unsigned long end = pit_ticks;
        unsigned long dt_ms = end - typing_start;
        if (dt_ms == 0) dt_ms = 1;

        int correct = str_eq(cmd, typing_text);

        if (!correct) {
            putstr(cursor, "Incorrect.", 0x0F);
            newline();
        } else {
            putstr(cursor, "Correct.", 0x0F);
            newline();
        }

        int len = 0;
        while (typing_text[len]) len++;

        unsigned int wpm = (unsigned int)((len * 60000UL) / (5UL * dt_ms));

        putstr(cursor, "Time: ", 0x0F);
        print_uint((unsigned int)dt_ms);
        putstr(cursor, " ms", 0x0F);
        newline();

        putstr(cursor, "Speed: ", 0x0F);
        print_uint(wpm);
        putstr(cursor, " WPM", 0x0F);
        newline();

        print_prompt();
        return;
    }
    /* This is the command dispatcher
       Have fun!
    */
    if (str_eq(cmd, "help")) {
        putstr(cursor, "Commands: help clear echo <text> demo type banner ram ls cat", 0x0F);
        newline();
    } else if (str_eq(cmd, "clear")) {
        draw_demo();
    } else if (str_startswith(cmd, "echo ")) {
        putstr(cursor, cmd + 5, 0x0F);
        newline();
    } else if (str_eq(cmd, "demo")) {
        color_demo();
    } else if (str_eq(cmd, "type")) {
        clear_screen(0x0F);
        putstr(cursor, "Typing test:", 0x0F);
        newline();
        putstr(cursor, typing_text, 0x0F);
        newline();
        newline();
        putstr(cursor, "Type the line above and press Enter.", 0x0F);
        newline();
        newline();
        print_prompt();
        shell_len = 0;
        typing_mode = 1;
        typing_start = pit_ticks;
        return;
    } else if (str_eq(cmd, "ram")) {
        uint32_t kb = lower + upper;

        print_uint(kb);
        putstr(cursor, " KB", 0x0F);
        newline();

        print_decimal(kb, 1024);
        putstr(cursor, " MB", 0x0F);
        newline();

        print_decimal(kb, 1024 * 1024);
        putstr(cursor, " GB", 0x0F);
        newline();

        print_prompt();
        return;
    } else if (str_eq(cmd, "banner")) {
        clear_screen(0x0F);
        print_banner();
    } else if (str_startswith(cmd, "cat ")) {
        const char* target = cmd + 4;

        int found = 0;
        for (int i = 0; i < initrd_file_count; i++) {
            if (str_eq(target, initrd_files[i].name)) {
                found = 1;
                for (uint32_t j = 0; j < initrd_files[i].size; j++) {
                    putch(cursor++, initrd_files[i].data[j], 0x0F);
                    if (cursor >= 80 * 25) {
                        scroll_up();
                        cursor = 24 * 80;
                    }
                }
                newline();
                break;
            }
        }

        if (!found) {
            putstr(cursor, "File not found", 0x0F);
            newline();
        }

    } else if (str_eq(cmd, "ls")) {
        clear_screen(0x0F);
        cursor = 0;

        newline();

        for (int i = 0; i < initrd_file_count; i++) {

            // force left edge
            if (cursor % 80 != 0)
                cursor = (cursor / 80 + 1) * 80;

            putstr(cursor, initrd_files[i].name, 0x0F);
            newline();
        }

    } else if (!str_eq(cmd, "")) {
        putstr(cursor, "Unknown command", 0x0F);
        newline();
    }
    print_prompt();
}

// Keyboard scancode handler
void keyboard_handler_c(void) {
    uint8_t sc = inb(0x60);

    if (sc & 0x80) {
        uint8_t make = sc & 0x7F;
        if (make == 0x2A || make == 0x36)
            shift_down = 0;
    } else {
        if (sc == 0x2A || sc == 0x36) {
            shift_down = 1;
            outb(0x20, 0x20);
            return;
        }

        if (sc == 0x3A) {
            caps_lock = !caps_lock;
            outb(0x20, 0x20);
            return;
        }

        char c = 0;
        if (sc < 128) {
            const char* map = shift_down ? keymap_shift : keymap_normal;
            c = map[sc];
        }

        if (c >= 'a' && c <= 'z') {
            if (caps_lock && !shift_down) c = c - 'a' + 'A';
            else if (caps_lock && shift_down) c = c - 'a' + 'A';
        }

        if (c == '\b') {
            if (shell_len > 0) {
                shell_len--;
                shell_buf[shell_len] = 0;
                cursor--;
                putch(cursor, ' ', 0x0F);
            }
        } else if (c == '\n') {
            shell_buf[shell_len] = 0;
            newline();
            handle_command(shell_buf);
            shell_len = 0;
        } else if (c) {
            if (shell_len < SHELL_BUF_SIZE - 1) {
                shell_buf[shell_len++] = c;
                putch(cursor++, c, 0x0F);
                if (cursor >= 80 * 25) {
                    scroll_up();
                    cursor = 24 * 80;
                }
            }
        }
    }

    outb(0x20, 0x20);
}

// Keyboard IRQ stub
__attribute__((naked))
void keyboard_stub(void) {
    __asm__ volatile (
        "pusha\n"
        "push %ds\n"
        "push %es\n"
        "push %fs\n"
        "push %gs\n"
        "mov $0x10, %ax\n"
        "mov %ax, %ds\n"
        "mov %ax, %es\n"
        "call keyboard_handler_c\n"
        "pop %gs\n"
        "pop %fs\n"
        "pop %es\n"
        "pop %ds\n"
        "popa\n"
        "iret\n"
    );
}

// Initialize IDT and PIC, install PIT and keyboard handlers
static void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;

    for (int i = 0; i < 256; i++)
        idt_set_entry(i, 0, 0x08, 0x8E);

    idt_set_entry(0x20, (uint32_t)pit_stub,      0x08, 0x8E); // PIT
    idt_set_entry(0x21, (uint32_t)keyboard_stub, 0x08, 0x8E); // keyboard

    __asm__ volatile ("lidt (%0)" :: "r"(&idtp));

    // PIC remap
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    // Unmask IRQ0 (PIT) and IRQ1 (keyboard)
    outb(0x21, 0xFC); // 11111100b
    outb(0xA1, 0xFF);

    __asm__ volatile ("sti");
}

// Kernel entry point
void kmain(void) {
    uint32_t magic;
    uint32_t mb_info_addr;

    __asm__ volatile ("movl %%eax, %0" : "=r"(magic));
    __asm__ volatile ("movl %%ebx, %0" : "=r"(mb_info_addr));

    multiboot_info_t* mb = (multiboot_info_t*)mb_info_addr;

    lower = mb->mem_lower;
    upper = mb->mem_upper;

    uint32_t initrd_start = 0;
    uint32_t initrd_end   = 0;

    if (mb->mods_count > 0) {
        multiboot_module_t* mods = (multiboot_module_t*)mb->mods_addr;
        initrd_start = mods[0].mod_start;
        initrd_end   = mods[0].mod_end;

        putstr(cursor, "Initrd loaded at: ", 0x0F);
        print_uint(initrd_start);
        newline();

        putstr(cursor, "Initrd size: ", 0x0F);
        print_uint(initrd_end - initrd_start);
        putstr(cursor, " bytes", 0x0F);
        newline();

        initrd_parse(initrd_start, initrd_end);
        // DEBUG: dump first 128 bytes of initrd
        putstr(cursor, "INITRD HEX DUMP:", 0x0F);
        newline();
        for (uint32_t i = 0; i < 128 && initrd_start + i < initrd_end; i++) {
            uint8_t b = ((uint8_t*)initrd_start)[i];
            const char* h = "0123456789ABCDEF";
            putch(cursor++, h[b >> 4], 0x0F);
            putch(cursor++, h[b & 0xF], 0x0F);
            putch(cursor++, ' ', 0x0F);
            if ((i % 16) == 15) newline();
        }
        newline();

        putstr(cursor, "Files parsed: ", 0x0F);
        print_uint(initrd_file_count);
        newline();
        if (initrd_file_count > 0) {
            putstr(cursor, "First file: '", 0x0F);
            putstr(cursor, initrd_files[0].name, 0x0F);
            putstr(cursor, "' size=", 0x0F);
            print_uint(initrd_files[0].size);
            newline();
        }
    }

    gdt_init();
    idt_init();
    pit_init();
    draw_demo();
    print_prompt();

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
