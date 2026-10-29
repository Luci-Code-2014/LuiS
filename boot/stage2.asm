[bits 16]
[org 0x7e00]

start:
    mov si, msg_stage2
    call print

.halt:
    cli
    hlt
    jmp .halt

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

; ===== 数据区 =====

msg_stage2 db "Stage2: hello from stage2! Meow~", 0x0d, 0x0a, 0

; ===== 填充 =====
times (4 * 512) - ($ - $$) db 0