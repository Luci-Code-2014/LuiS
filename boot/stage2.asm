[bits 16]
[org 0x7e00]

CODE_SEG   equ gdt_code - gdt_start
DATA_SEG   equ gdt_data - gdt_start
CODE64_SEG equ gdt_code64 - gdt64_start
DATA64_SEG equ gdt_data64 - gdt64_start

start:
    mov si, msg_stage2
    call print

    in al, 0x92
    or al, 2
    out 0x92, al

    cli
    xor ax, ax
    mov ds, ax
    lgdt [gdt32_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp CODE_SEG:init_32bit

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

msg_stage2 db "Stage2: entering protected mode ...", 0x0d, 0x0a, 0

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
    mov ecx, 12288
    cld
    rep stosb

    mov edi, 0x1000
    mov eax, 0x2000
    or eax, 0x03
    mov [edi], eax

    mov edi, 0x2000
    mov eax, 0x3000
    or eax, 0x03
    mov [edi], eax

    mov edi, 0x3000
    mov eax, 0x83
    mov ecx, 512
.loop_pd:
    mov [edi], eax
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

    ; 确保在开启分页前加载 GDT64
    lgdt [gdt64_descriptor]

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ; 最标准可靠的远跳转
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
    dd gdt64_start   ; 在32位段加载时，用dd只占4字节

[bits 64]
init_64bit:
    ; 进入64位第一件事：设置栈
    mov ax, DATA64_SEG
    mov ss, ax
    mov rsp, 0x90000

    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    cld
    mov rsi, msg_stage64
.print64:
    lodsb
    test al, al
    jz .done64
    mov dx, 0x3F8
    out dx, al
    jmp .print64
.done64:
    jmp $

msg_stage64 db "SUCCESS: LuiS is now in 64-bit Long Mode!", 0x0d, 0x0a, 0

times (16 * 512) - ($ - $$) db 0