; kernel/isr.asm
[bits 64]

%macro ISR_NOERR 1
global isr%1
isr%1:
    push qword 0          ; 假错误码
    push qword %1         ; 中断号
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push qword %1         ; 中断号 (错误码已由 CPU 压入)
    jmp isr_common
%endmacro

; CPU 自动压错误码的异常
ISR_ERR 8
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_ERR 17
ISR_ERR 21
ISR_ERR 29
ISR_ERR 30

; 其余异常: 0-7, 9, 15, 16, 18, 19, 20, 22-28, 31
%assign i 0
%rep 32
    %if i != 8 && i != 10 && i != 11 && i != 12 && i != 13 && i != 14 && i != 17 && i != 21 && i != 29 && i != 30
        ISR_NOERR i
    %endif
    %assign i i+1
%endrep

; IRQ 0-15 (PIC 重映射后是 32-47)
%assign i 32
%rep 16
    ISR_NOERR i
    %assign i i+1
%endrep

; 其余保留中断 48-255
%assign i 48
%rep 208
    ISR_NOERR i
    %assign i i+1
%endrep

; 公共入口: 保存寄存器, 调 C 处理函数
extern isr_handler_c
isr_common:
    ; 依次压入通用寄存器 (顺序要和 struct interrupt_frame 一致)
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp          ; 第一个参数: 指向 interrupt_frame
    cld                   ; 清方向标志, C 代码要求
    call isr_handler_c

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16           ; 弹出 int_no 和 err_code
    iretq

global isr_stub_table
section .data
isr_stub_table:
%assign i 0
%rep 256
    dq isr%+i
    %assign i i+1
%endrep