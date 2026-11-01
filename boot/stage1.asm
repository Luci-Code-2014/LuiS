[bits 16]
[org 0x7c00]

STAGE2_SECTORS equ 16
STAGE2_ADDR    equ 0x7e00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti

    mov [boot_drive], dl
    mov [0x0500], dl            ; 给 stage2 传盘号

    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .disk_error

    jmp STAGE2_ADDR

.disk_error:
    mov si, msg_err
    call print
    cli
    hlt
    jmp $

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

boot_drive db 0
msg_err    db "Stage1: disk read error!", 0x0d, 0x0a, 0

align 4
dap:
    db 0x10
    db 0x00
    dw STAGE2_SECTORS
    dw STAGE2_ADDR
    dw 0x0000
    dq 1

times 510-($-$$) db 0
dw 0xaa55