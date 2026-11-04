[bits 64]
[default rel]

global _start
extern kernel_main
extern __bss_start
extern __bss_end

section .text.entry
_start:
    mov rsp, 0x90000
    xor rbp, rbp

    lea rdi, [__bss_start]
    lea rcx, [__bss_end]
    sub rcx, rdi
    xor al, al
    cld
    rep stosb

    call kernel_main

.hang:
    cli
    hlt
    jmp .hang