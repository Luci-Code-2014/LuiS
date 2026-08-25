[bits 16] ; 让 CPU 工作到 16 位模式
[org 0x7C00] ; 标准: 让内存地址计算从 0x7C00 开始

; === 定义 ===

; equ = #define / alias
kernel_offset equ 0x1000 ; kernel_offset = 0x1000
code_segment equ 0x08 ; code_segment = 0x08
data_segment equ 0x10 ; data_segment = 0x10
code64_segment equ 0x18 ; code64_segment = 0x18

; === GDT 段 ===

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

gdt_code64: ; === 64 位的 GDT ===
    dw 0xFFFF      ; 段限长低 16 位
    dw 0x0000      ; 段基址低 16 位
    db 0x00        ; 段基址 16-23 位
    db 0x9A        ; 可读可执行
    db 0xAF        ; 粒度 4KB + 64 位标志 (L=1)
    db 0x00        ; 段基址 24-31 位

gdt_end: ; GDT 结束位置, 用来计算 GDT 大小

gdt_descriptor: ; GDT 指针
    dw gdt_end - gdt_start - 1  ; GDT 大小 - 1
    dd gdt_start                ; GDT 起始地址

; === 数据段 ===

; 模板:
; 数据段名称
;     db "数据", 0x0D, 0x0A, 0x00

; 意思:
; db = define byte
; 0x0D = 回车
; 0x0A = 换行
; 0x00 = 结束符

msg:
    db "HelloWorld", 0x0D, 0x0A, 0x00

msg_no64:
    db "Your CPU does not support 64-bit", 0x0D, 0x0A, 0x00

; === 入口段 ===

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

    ; === 切换到 64 位长模式 (这很重要)
    ; 1.检查 CPU 是否支持 64 位
    ; 方法: 尝试翻转 EFLAGS 的 ID 位 (bit 21)

    pushfd ; 保存 EFLAGS 到栈
    pop eax ; eax = EFLAGS
    mov ecx, eax ; ecx = 备份原来的 EFLAGS
    xor eax, 1 << 21 ; 翻转 ID 位
    push eax ; 把修改过后的值压回栈
    popfd ; 加载到 EFLAGS (尝试修改)
    pushfd ; 再读回 EFLAGS
    pop eax ; eax = 修改后的 EFLAGS
    push ecx ; 恢复原来的 EFLAGS
    popfd ; 还原
    cmp eax, ecx ; 比较是否修改成功
    je no_long_mode ; 如果没变 -> CPUID 不支持 -> 跳转

    ; 2.检查扩展功能是否支持
    ; CPUID 叶子 0x80000000 返回最高支持的扩展叶子

    mov eax, 0x80000000 ; 扩展功能号
    cpuid ; 调用 CPUID
    cmp eax, 0x80000001 ; 检查是否支持 0x80000001
    jb no_long_mode ; 如果不支持就跳转不支持 64 位的报错

    ; 3.检查长模式标志
    ; CPUID 叶子 0x80000001 的 edx bit 29 = 长模式支持

    mov eax, 0x80000001 ; 扩展功能好: 长模式 (64 位) 检测
    cpuid ; 调用 CPUID
    test edx, 1 << 29 ; 测试 edx 的 bits 29
    jz no_long_mode

    ; - 可以使用 64 位的情况
    jmp long_mode_ok ; 跳转到切换 64 位

print: ; 标签 print
    lodsb ; Load string byte | 加载字符串字节
    or al, al ; 位或运算
    jz .done ; 如果 al 为 0 就跳转 .done
    mov ah, 0x0E ; ah = ax 的高 8 位 (high) | 0x0E = BIOS 的输出功能号
    int 0x10 ; svc #0
    jmp print ; jmp = jump | 无条件跳转

no_long_mode: ; CPU 不支持 64 位时
    mov si, msg_no64 ; 能输出
    call print ; 调用 print 标签
    cli ; CPU 防中断
    hlt ; 停机

long_mode_ok: ; CPU 支持 64 位时

    xor ax, ax ; 把 ax 寄存器清零
    mov es, ax ; 把 es 寄存器写入 ax (其实也是清零)

    ; === 切换 64 位的代码 ===

    ; 1.开 PAE (物理地址扩展)
    mov eax, cr4 ; 把 cr4 寄存器的值存入 eax
    or eax, 1 << 5 ; 把 eax 的第 5 位设为 1 (PAE 位)
    mov cr4, eax ; 把 eax 写回 cr4，开启 PAE

    ; 2.建页表
    mov edi, 0x9000 ; 把页表起始地址 0x9000 放进 edi 寄存器
    xor eax, eax ; 把 eax 清零
    mov ecx, 4096 / 4 ; ecx = 1024 (4 KB / 4 byte)
    rep stosd ; 把 eax 的值 (0) 重复写入 104 次, 清空 0x90

    ; - PML 4 (第一级)
    mov dword [0x9000], 0x9103 ; 把 0x9103 写入 PML4[0]
    mov dword [0x9000 + 4], 0x0000 ; 把 PML4[0] 的高 32 位清零

    ; - PDPT (第二级)
    mov dword [0x9100], 0x9203 ; 把 0x9203 写入 PDPT[0]
    mov dword [0x9100 + 4], 0x0000 ; 把 PDPT[0] 的高 32 位清零

    ; - PDT (第三级)
    mov dword [0x9200], 0x9303 ; 把 0x9303 写入 PDT[0]
    mov dword [0x9200 + 4], 0x0000 ; 把 PDT[0] 的高 32 位清零

    ; - PT (第四级)
    mov dword [0x9300], 0x83 ; 把 0x83 写入 PT[0]
    mov dword [0x9300 + 4], 0x0000 ; 把 PT[0] 的高 32 位清零

    ; 3.加载页表地址到 cr3
    mov eax, 0x9000 ; 把 PML4 的物理地址 0x9000 放入 eax
    mov cr3, eax ; 把 eax 写入 cr3, 告诉 CPU 页表在哪

    ; 4.开长模式 (64 位)
    mov ecx, 0xC0000080 ; 把 MSR 地址 (EFER) 放入 ecx
    rdmsr ; 读 EFER 到 eax (edx 是保留的)
    or eax, 1 << 8 ; 把 eax 的第 8 位设为 1 (LME 位)
    wrmsr ; 把 eax 写回 EFER, 开启长模式

    ; 5.开分页
    mov eax, cr0 ; 把 cr0 寄存器的值读入 eax
    or eax, 1 << 31 ; 把 eax 的第 31 位设为 1 (PG 位)
    mov cr0, eax ; 把 eax 写回 cr0, 开启分页

    ; 6.跳转内核
    jmp 0x18:0x10000 ; code64_segment = 0x18

.done: ; 局部标签
    ret ; 返回, 相当于退出

times 510 - ($ - $$) db 0 ; 把 db 0 执行 byte 次 | byte = (整个文件的结尾 - 整个文件的开头)
dw 0xAA55 ; dw = define word | 0xAA55 = 引导扇区签名, BIOS 靠这个认这是可识别的磁盘