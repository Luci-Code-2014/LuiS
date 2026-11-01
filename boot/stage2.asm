[bits 16]
[org 0x7e00]

CODE_SEG        equ gdt_code - gdt_start
DATA_SEG        equ gdt_data - gdt_start
CODE64_SEG      equ gdt_code64 - gdt64_start
DATA64_SEG      equ gdt_data64 - gdt64_start
KERNEL_SECTORS  equ 8
KERNEL_LBA      equ 17
KERNEL_ADDR     equ 0x10000      ; <- 改成 64KB 低地址
KERNEL_DRIVE    equ 0x0500

start:
    in al, 0x92
    test al, 2
    jnz .a20_on
    or al, 2
    and al, 0x0FE
    out 0x92, al

.a20_on:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax

    mov si, dap_kernel
    mov dl, [KERNEL_DRIVE]
    mov ah, 0x42
    int 0x13
    jc .kernel_error

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

align 4
dap_kernel:
    db 0x10
    db 0x00
    dw KERNEL_SECTORS
    dw 0x0000                ; 偏移 0x0000
    dw 0x1000                ; 段 0x1000 -> 0x1000*16 + 0x0 = 0x10000
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

    mov edi, 0x1000
    mov cr3, edi
    xor eax, eax
    mov ecx, 3072
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

    mov edi, 0x3000
    mov eax, 0x83
    mov ecx, 512
.loop_pd:
    mov dword [edi], eax
    mov dword [edi+4], 0
    add eax, 0x200000
    add edi, 8
    loop .loop_pd

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

    ; ---- 跳转 Kernel ----
    mov rax, KERNEL_ADDR
    jmp rax

times (16 * 512) - ($ - $$) db 0