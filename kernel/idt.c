// kernel/idt.c
#include "idt.h"
#include "isr.h"
#include "pic.h"
#include "keyboard.h"
#include "pit.h"
#include "mouse.h"
#include "panic.h"

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
    // 如果触发的是 CPU 异常 (0 ~ 31)
    if (f->int_no < 32) {
        // 根据异常号找异常名字 (你之前写的 exc_name 数组)
        extern const char *exc_name[];
        panic(exc_name[f->int_no], f->err_code, f->rip, f->rsp);
    }

    // IRQ 32 ~ 47 保持原样
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