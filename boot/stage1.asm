[bits 16]
[org 0x7c00]

stage2_sectors equ 4      ; Stage2 占 4 个扇区
stage2_addr    equ 0x7e00 ; Stage2 加载到内存的目标地址 (0x7c00 + 0x200)

start:
    cli                         ; 关闭中断
    xor ax, ax                  ; ax = 0
    mov ds, ax                  ; 数据段 (data segment) = 0
    mov es, ax                  ; 附加段 (extra segment) = 0
    mov ss, ax                  ; 栈段 (stack segment) = 0
    mov sp, 0x7c00
    sti                         ; 开中断

    mov [boot_drive], dl        ; BIOS 进入时 dl = 启动盘号

    mov si, msg_stage1          ; si 指向字符串
    call print                  ; 调用打印子程序

    mov ah, 0x02                ; ah = 02 表示读扇区
    mov al, stage2_sectors      ; 要读取的扇区数
    mov ch, 0                   ; 柱面号 = 0
    mov cl, 2                   ; 起始柱面号 = 2
    mov dh, 0                   ; 磁头号
    mov dl, [boot_drive]        ; 盘号
    mov bx, stage2_addr         ; 目标偏移
    int 0x13                    ; 调用 BIOS 磁盘中断
    jc .disk_error              ; CF = 1 表示出错

    jmp stage2_addr             ; 读取成功直接跳进 Stage2 执行

.disk_error:
    mov si, msg_err             ; 输出错误信息
    call print
    cli                         ; 关中断
    hlt                         ; 停机
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

; ===== 数据区 =====

boot_drive db 0
msg_stage1 db "Stage1: Loading Stage2", 0x0d, 0x0a, 0
msg_err    db "Stage1: disk read error!", 0x0d, 0x0a, 0

; ===== 填充 + 引导签名 =====

times 510-($-$$) db 0
dw 0xaa55