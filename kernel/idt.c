// kernel/idt.c
#include "idt.h"

extern void *isr_stub_table[256];
extern void isr_handler_c(struct interrupt_frame *frame);

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

static void idt_set(int vec, void *handler) {
    uint64_t addr = (uint64_t)handler;
    idt[vec].offset_low  = addr & 0xFFFF;
    idt[vec].selector    = 0x08;          // 内核代码段
    idt[vec].ist         = 0;
    idt[vec].type_attr   = 0x8E;          // present, DPL=0, 64-bit interrupt gate
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

/* C 层统一处理 */
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
    /* IRQ 处理以后再加 */
}