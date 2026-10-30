[bits 16]
[org 0x7e00]

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

start:
    mov si, msg_stage2
    call print

    in al, 0x92
    or al, 2
    out 0x92, al

    cli
    xor ax, ax
    mov ds, ax
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:protected_mode

print:
    pusha
.loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0e
    int 0x10
    jmp .loop
.done:
    popa
    ret

gdt_start:
    dq 0x0000000000000000
gdt_code:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dw gdt_start
    db 0x00

msg_stage2 db "Stage2: entering protected mode ...", 0x0d, 0x0a, 0

[bits 32]
protected_mode:
    mov ax, DATA_SEG
    mov ss, ax
    mov esp, 0x90000
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov dx, 0x3F8
    mov al, 'X'
    out dx, al

    mov esi, msg_pm
.print_pm:
    lodsb
    test al, al
    jz .done_pm
    mov dx, 0x3F8
    out dx, al
    jmp .print_pm
.done_pm:
    jmp $

msg_pm db " Meow! We are in 32-bit protected mode!", 0

times (4 * 512) - ($ - $$) db 0