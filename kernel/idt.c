// kernel/idt.c
#include "idt.h"
#include "isr.h"
#include "pic.h"
#include "keyboard.h"
#include "pit.h"
#include "mouse.h"

extern void *isr_stub_table[256];

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

static void idt_set(int vec, void *handler) {
    uint64_t addr = (uint64_t)handler;
    idt[vec].offset_low  = addr & 0xFFFF;
    idt[vec].selector    = 0x08;
    idt[vec].ist         = 0;
    idt[vec].type_attr   = 0x8E;
    idt[vec].offset_mid  = (addr >> 16) & 0xFFFF;
    idt[vec].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vec].zero        = 0;
}

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint64_t)&idt;

    for (int i = 0; i < 256; i++) {
        idt_set(i, isr_stub_table[i]);
    }

    __asm__ volatile ("lidt %0" : : "m"(idtp));
}

/* 异常名称表 */
static const char *exc_name[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound Range", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection", "Page Fault", "Reserved",
    "x87 FP", "Alignment Check", "Machine Check", "SIMD FP",
    "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection", "VMM Communication", "Security", "Reserved"
};

void isr_handler_c(struct interrupt_frame *f) {
    extern void printk(const char *, ...);

    if (f->int_no < 32) {
        /* 异常处理 */
        printk("\n*** EXCEPTION %d: %s ***\n",
            (int)f->int_no, exc_name[f->int_no]);
        printk("  err_code = %p\n", (void *)f->err_code);
        printk("  RIP = %p  CS = %p  RFLAGS = %p\n",
            (void *)f->rip, (void *)f->cs, (void *)f->rflags);
        printk("  RSP = %p  SS = %p\n", (void *)f->rsp, (void *)f->ss);
        printk("  RAX=%p RBX=%p RCX=%p RDX=%p\n",
            (void *)f->rax, (void *)f->rbx, (void *)f->rcx, (void *)f->rdx);
        printk("  RSI=%p RDI=%p RBP=%p\n",
            (void *)f->rsi, (void *)f->rdi, (void *)f->rbp);
        printk("*** HALT ***\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    /* IRQ 32~47 */
    if (f->int_no >= 32 && f->int_no < 48) {
        unsigned char irq = f->int_no - 32;

        if (irq == 1) {
            keyboard_handle_irq();
        } else if (irq == 12) {
            mouse_handle_irq();
        }

        pic_send_eoi(irq);
    }
}