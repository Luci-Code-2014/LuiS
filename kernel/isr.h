// kernel/isr.h
#ifndef ISR_H
#define ISR_H

#include <stdint.h>

/* 中断帧: isr.asm 压栈顺序 + CPU 自动压入的 5 项
 * 字段顺序必须和 isr_common 里 push 的顺序严格对应 (从低地址到高地址)
 */
struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

/* isr.asm 导出的 256 个 stub 地址表 */
extern void *isr_stub_table[256];

/* C 层统一处理函数, 由 isr_common 调用 */
void isr_handler_c(struct interrupt_frame *frame);

#endif