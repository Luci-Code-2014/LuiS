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

    mov si, msg_stage1
    call print

    ; ---- 使用 LBA 扩展读取 ----
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

; ---- 数据区 ----
boot_drive db 0
msg_stage1 db "Stage1: Loading Stage2...", 0x0d, 0x0a, 0
msg_err    db "Stage1: disk read error!", 0x0d, 0x0a, 0

; 磁盘地址包 (Disk Address Packet)
align 4
dap:
    db 0x10                 ; 结构体大小
    db 0x00                 ; 保留
    dw STAGE2_SECTORS       ; 要读取的扇区数 (16)
    dw STAGE2_ADDR          ; 目标偏移 (0x7e00)
    dw 0x0000               ; 目标段基址 (0)
    dq 1                    ; 起始 LBA 地址 (第 2 个扇区, LBA 从 0 开始算)

times 510-($-$$) db 0
dw 0xaa55