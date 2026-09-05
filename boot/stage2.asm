[bits 16]
[org 0x0000]

; === 定义 ===

STAGE2_SECTORS equ 10
KERNEL_SECTORS equ 64
kernel_offset equ 0x1000
code_segment equ 0x08
data_segment equ 0x10
code64_segment equ 0x18
PML4 equ 0x9000
PDPT equ 0xA000
PD equ 0xB000

_start:
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0xFFFF

    mov si, msg_stage2_ok
    call print

    ; === 加载内核 ===
    mov ax, 0x2000
    mov es, ax
    xor bx, bx

    mov ah, 0x02
    mov al, KERNEL_SECTORS
    mov ch, 0
    mov cl, 2 + STAGE2_SECTORS
    mov dh, 0
    mov dl, 0x00
    int 0x13
    jc error

    mov si, msg_kernel_ok
    call print

    ; === 开 A20 ===
    in al, 0x92
    or al, 0x02
    out 0x92, al

    ; === 加载 GDT ===
    lgdt [gdt_descriptor]

    ; === 开保护模式 ===
    mov eax, cr0
    or eax, 0x01
    mov cr0, eax
    ; 立即远跳转刷新 CS, 进入 32 位保护模式代码
    jmp 0x08:protected_mode

no_long_mode:
    mov si, msg_no64
    call print
    cli
    hlt

protected_mode:
    ; 设置数据段选择子 (使用 GDT 中的 0x10 描述符)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x90000          ; 将栈设置在页表上方安全区域

    ; === 检查 CPU 是否支持 64 位长模式 ===
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21          ; 尝试翻转 ID 位
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd                     ; 恢复原 EFLAGS
    cmp eax, ecx
    je no_long_mode           ; 若 ID 位无法翻转, 不支持 CPUID

    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001       ; 检查最大扩展功能号
    jb no_long_mode

    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29         ; 检查 LM 位
    jz no_long_mode

    ; === 开启 PAE (Physical Address Extension) ===
    mov eax, cr4
    or eax, 1 << 5            ; CR4.PAE = 1
    mov cr4, eax

    ; === 建立页表 (映射物理地址 0~2MB, 使用 2MB 大页) ===
    ; 页表基址: PML4 = 0x9000, PDPT = 0xA000, PD = 0xB000
    mov edi, PML4
    xor eax, eax
    mov ecx, 4096 / 4
    rep stosd                 ; 清零 PML4 表 (4 KB)

    mov dword [PML4], PDPT | 0x3   ; PML4[0] 指向 PDPT (0xA000), 属性: 存在+可写
    mov dword [PDPT], PD   | 0x3   ; PDPT[0] 指向 PD (0xB000), 属性: 存在+可写
    mov dword [PD],   0x83         ; PD[0] 为 2MB 大页, 映射物理地址 0x00000000, 属性: 存在+可写+PS

    mov eax, PML4
    mov cr3, eax             ; 加载页表基址到 CR3

    ; === 开启长模式 (EFER.LME) ===
    mov ecx, 0xC0000080      ; EFER MSR 地址
    rdmsr
    or eax, 1 << 8           ; 设置 LME 位
    wrmsr

    ; === 开启分页 (CR0.PG) ===
    mov eax, cr0
    or eax, 1 << 31          ; CR0.PG = 1
    mov cr0, eax

    ; === 远跳转到 64 位内核 ===
    ; 此时 CS 仍为 32 位代码段，通过 0x18 选择子进入 64 位代码段
    jmp 0x18:0x20000         ; 内核物理地址 0x20000

error:
    mov si, msg_error
    call print
    cli
    hlt

print:
    lodsb
    or al, al
    jz .done
    mov dx, 0x3F8
    out dx, al
    mov dx, 0x3FD
.wait:
    in al, dx
    test al, 0x20
    jz .wait
    jmp print
.done:
    ret

msg_stage2_ok: db "[Stage2 loaded]", 0x0D, 0x0A, 0
msg_kernel_ok: db "[Kernel loaded]", 0x0D, 0x0A, 0
msg_no64: db "[CPU does not support 64-bit]", 0x0D, 0x0A, 0
msg_error: db "[Kernel load error]", 0x0D, 0x0A, 0

; === GDT ===
gdt_start:
    dd 0x00000000
    dd 0x00000000

gdt_code:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xCF
    db 0x00

gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92
    db 0xCF
    db 0x00

gdt_code64:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xAF
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 5120 - ($ - $$) db 0

; 内核占位符: 64 扇区 (32768 字节), 填充为死循环 jmp $
%rep 16384
    db 0xEB, 0xFE
%endrep