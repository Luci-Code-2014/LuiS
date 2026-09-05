[bits 64]

global keyboard_handler
global load_idt
global enable_keyboard

keyboard_handler:
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

    in al, 0x60
    mov rdi, rax
    call keyboard_callback

    mov al, 0x20
    out 0x20, al

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
    
    iretq

load_idt:
    lidt [rdi]
    ret

enable_keyboard:
    in al, 0x21
    and al, 0xFD
    out 0x21, al
    ret