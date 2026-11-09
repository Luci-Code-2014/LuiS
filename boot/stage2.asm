[bits 16]
[org 0x7e00]

CODE_SEG        equ gdt_code - gdt_start
DATA_SEG        equ gdt_data - gdt_start
CODE64_SEG      equ gdt_code64 - gdt64_start
DATA64_SEG      equ gdt_data64 - gdt64_start

; 每次读 16 扇区 (8KB), 循环 16 次, 总共 256 扇区 (128KB)
KERNEL_SECTORS  equ 16
KERNEL_LOOPS    equ 16
KERNEL_LBA      equ 17
KERNEL_ADDR     equ 0x10000
KERNEL_DRIVE    equ 0x0500

start:
    ; ===== 1. 开启 A20 =====
    in al, 0x92
    test al, 2
    jnz .a20_done
    or al, 2
    and al, 0x0FE
    out 0x92, al
.a20_done:

    ; ===== 2. 设置 VBE 模式 1024x768x32 =====
    mov ax, 0x4F02
    mov bx, 0x4118
    int 0x10
    cmp ax, 0x004F
    jne .vbe_error

    ; ===== 3. 获取 VBE 模式信息 =====
    mov ax, 0x4F01
    mov cx, 0x118
    mov di, vbe_mode_info
    int 0x10
    cmp ax, 0x004F
    jne .vbe_error

    ; 存 framebuffer 地址到 0x5000
    mov eax, [vbe_mode_info + 0x28]
    mov [0x5000], eax
    mov ax, [vbe_mode_info + 0x10]
    movzx eax, ax
    mov [0x5004], eax
    mov al, [vbe_mode_info + 0x19]
    movzx eax, al
    mov [0x5008], eax

    jmp .vbe_ok

.vbe_error:
    mov si, msg_vbe_err
    call print
    cli
    hlt
    jmp $

.vbe_ok:
    ; ===== 4. 循环读内核 =====
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax

    mov cx, KERNEL_LOOPS
    mov word [dap_kernel_seg], 0x1000
    mov dword [dap_kernel_lba], KERNEL_LBA

.read_kernel_loop:
    push cx
    mov si, dap_kernel
    mov dl, [KERNEL_DRIVE]
    mov ah, 0x42
    int 0x13
    jc .kernel_error

    ; 每次读完, 段地址前进 8KB (0x200), LBA 前进 16
    add word [dap_kernel_seg], 0x200
    add dword [dap_kernel_lba], 16

    pop cx
    loop .read_kernel_loop

    ; ===== 5. 进保护模式 =====
    lgdt [gdt32_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp CODE_SEG:init_32bit

.kernel_error:
    mov si, msg_kernel_err
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

; ===== 数据 =====
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

gdt32_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

msg_kernel_err db "Stage2: kernel disk read error!", 0x0d, 0x0a, 0
msg_vbe_err    db "Stage2: VBE mode set failed!", 0x0d, 0x0a, 0

align 4
vbe_mode_info:
    times 256 db 0

align 4
dap_kernel:
    db 0x10
    db 0x00
    dw KERNEL_SECTORS
dap_kernel_off:
    dw 0x0000
dap_kernel_seg:
    dw 0x1000
dap_kernel_lba:
    dq KERNEL_LBA

[bits 32]
init_32bit:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode

    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode

    ; ===== 建页表 =====
    mov edi, 0x1000
    mov cr3, edi
    xor eax, eax
    mov ecx, 4096
    cld
    rep stosd

    mov edi, 0x1000
    mov eax, 0x2000
    or eax, 0x03
    mov dword [edi], eax
    mov dword [edi+4], 0

    mov edi, 0x2000
    mov eax, 0x3000
    or eax, 0x03
    mov dword [edi], eax
    mov dword [edi+4], 0

    mov edi, 0x2000 + (3 * 8)
    mov eax, 0x4000
    or eax, 0x03
    mov dword [edi], eax
    mov dword [edi+4], 0

    ; 填充 PD0: 映射 0 - 1GB
    mov edi, 0x3000
    mov eax, 0x83
    mov ecx, 512
.loop_pd0:
    mov dword [edi], eax
    mov dword [edi+4], 0
    add eax, 0x200000
    add edi, 8
    loop .loop_pd0

    ; 填充 PD3: 映射 3GB - 4GB
    mov edi, 0x4000
    mov eax, 0xC0000083
    mov ecx, 512
.loop_pd3:
    mov dword [edi], eax
    mov dword [edi+4], 0
    add eax, 0x200000
    add edi, 8
    loop .loop_pd3

    ; ===== 开启 PAE, 长模式 =====
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    lgdt [gdt64_descriptor]

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    jmp CODE64_SEG:init_64bit

.no_long_mode:
    jmp $

align 8
gdt64_start:
    dq 0x0000000000000000
gdt_code64:
    dw 0x0000
    dw 0x0000
    db 0x00
    db 10011010b
    db 00100000b
    db 0x00
gdt_data64:
    dw 0x0000
    dw 0x0000
    db 0x00
    db 10010010b
    db 00000000b
    db 0x00
gdt64_end:

gdt64_descriptor:
    dw gdt64_end - gdt64_start - 1
    dq gdt64_start

[bits 64]
init_64bit:
    mov ax, DATA64_SEG
    mov ss, ax
    mov rsp, 0x90000
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    cld
    mov rax, KERNEL_ADDR
    jmp rax

times (16 * 512) - ($ - $$) db 0