[bits 32] ; 32 位
[extern _kernel_main] ; 有个叫 kernel_main 的函数在另一个文件内

global _start ; 全局: _start
_start: ; 程序入口点
    mov esp, 0x90000 ; 给 kernel.c 一个栈
    call _kernel_main ; 调用 kernel.c 里面 kernel_main 函数
    cli ; clear interrupt
    hlt ; halt