[bits 16] ; 让 CPU 工作到 16 位模式
[org 0x7C00] ; 标准: 让内存地址计算从 0x7C00 开始

; equ = #define / alias
kernel_offset equ 0x1000 ; kernel_offset = 0x1000
code_segment equ 0x08 ; code_segment = 0x08
data_segment equ 0x10 ; data_segment = 0x10

gdt_start: ; === GDT ===
    dd 0x00000000 ; 必须空
    dd 0x00000000 ; 也是

gdt_code: ; === GDT code ===
    dw 0xFFFF    ; 段限长 0-15 位 (全 1)
    dw 0x0000    ; 段基址 0-15 位 (从 0 开始)
    db 0x00      ; 段基址 16-23 位
    db 0x9A      ; 权限; 可读可执行
    db 0xCF      ; 粒度 4KB + 32 位模式 + 段限长高 4 位 (全 1)
    db 0x00      ; 段基址 24-31 位

gdt_data: ; === GDT data ===
    dw 0xFFFF    ; 段限长 0-15 位 (全 1)
    dw 0x0000    ; 段基址 0-15 位 (从 0 开始)
    db 0x00      ; 段基址 16-23 位
    db 0x92      ; 权限; 可读可写
    db 0xCF      ; 粒度 4KB + 32 位模式 + 段限长高 4 位 (全 1)
    db 0x00      ; 段基址 24-31 位

gdt_end: ; GDT 结束位置, 用来计算 GDT 大小

gdt_descriptor: ; GDT 指针
    dw gdt_end - gdt_start - 1  ; GDT 大小 - 1
    dd gdt_start                ; GDT 起始地址

msg: ; 数据段名称
    db "HelloWorld", 0x0D, 0x0A, 0x00 ; "HelloWorld"
    ; db = define byte
    ; 0x0D = 回车
    ; 0x0A = 换行
    ; 0x00 = 结束符

start: ; 程序入口点
    mov ax, 0x0003 ; ax 是 CPU 内部 16 位下通用临时寄存器 | 0x0003 是 80*25 的最通用彩色字符
    int 0x10 ; BIOS 的视频服务中断号, 没有这个啥都显示不了
    mov si, msg ; si = Source index
    call print ; 调用 print
    mov ax, kernel_offset ; 把 0x1000 加载到 ax 寄存器
    mov es, ax ; es = 附加段寄存器
    xor bx, bx ; 把 bx 清零
    mov ah, 0x02 ; ah 是告诉 BIOS 你要干什么 | 0x02 是读取磁盘扇区
    mov al, 33 ; 读 33 个扇区
    mov ch, 0 ; 告诉 BIOS 从磁盘的 0 开始读
    mov cl, 2 ; 读 kernel.c
    mov dh, 0 ; 从第 0 个磁头开始
    mov dl, 0x80 ; 从启动硬盘读数据
    int 0x13 ; 磁盘中断
    in al, 0x92 ; 读取键盘控制器状态
    or al, 0x02 ; 设置 A20 位
    out 0x92, al ; 写回
    lgdt [gdt_descriptor] ; 告诉 CPU GDT 在哪里
    mov eax, cr0 ; 读取控制寄存器
    or eax, 0x01 ; 设置 PE 位
    mov cr0, eax ; 写回, 开启保护
    jmp 0x08:0x11000 ; 跳转到内核入口（0x11000）

print: ; 标签 print
    lodsb ; Load string byte | 加载字符串字节
    or al, al ; 位或运算
    jz .done ; 如果 al 为 0 就跳转 .done
    mov ah, 0x0E ; ah = ax 的高 8 位 (high) | 0x0E = BIOS 的输出功能号
    int 0x10 ; svc #0
    jmp print ; jmp = jump | 无条件跳转

.done: ; 局部标签
    ret ; 返回, 相当于退出

times 510 - ($ - $$) db 0 ; 把 db 0 执行 byte 次 | byte = (整个文件的结尾 - 整个文件的开头)
dw 0xAA55 ; dw = define word | 0xAA55 = 引导扇区签名, BIOS 靠这个认这是可识别的磁盘