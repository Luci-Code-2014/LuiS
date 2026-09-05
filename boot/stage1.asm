[bits 16]
[org 0x7C00]

start:
    cli
    cld
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0xFFFF
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov ax, 0x0003
    int 0x10
    mov si, msg_loading
    call print

    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 10
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, 0x00
    int 0x13
    jc error

    jmp 0x1000:0x0000

error:
    mov si, msg_error
    call print
    cli
    hlt

print:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print
.done:
    ret

msg_loading: db "Loading stage2...", 0x0D, 0x0A, 0
msg_error: db "Disk error!", 0x0D, 0x0A, 0

times 510 - ($ - $$) db 0
dw 0xAA55