# LuiS Change log

| 介绍 | 版本 | 当前 |
| --- | --- | --- |
| kernel(ata,ext4,rw): 读写 rootfs.img (fs/rootfs.img) | 2.6.0 | * |
| kernel(ata,ext4): 添加 ATA 驱动, 仅读取 rootfs.img, ext4 | 2.5.0 |   |
| fix: 修改了 README.md 的编译要求字段, 重新提交了 Stage2 | 2.4.1 |   |
| kernel(kmalloc): 新增 kmalloc, 支持内存管理 | 2.4.0 |   |
| kernel(panic): 新增 panic 函数, panic 时仍支持鼠标 | 2.3.0 |   |
| kernel(mouse): 支持鼠标. 经典 Windows 款式 | 2.2.0 |   |
| kernel(font): 可渲染文字, 使用 kernel/font8x16.h | 2.1.0 |   |
| kernel(gfx): 添加了图形化 | 2.0.0 |   |
| kernel(keyboard): 添加了 PS/2 键盘和 IRQ1 处理和扫描码映射 | 1.2.0 |   |
| kernel(idt): 添加了 IDT | 1.1.0 |   |
| kernel: 为 printk 函数添加了一堆格式化标识符 | 1.0.2 |   |
| kernel: 为 printk 函数添加 %d | 1.0.1 |   |
| feat: 内核加载成功! | 1.0.0 |   |
| fix: 修正 stage2 中 cpuid 拼写, PML4/PDPT 页表项写入, A20 标签位置及 8 字节页表项写入 | 0.1.2 |   |
| feat: 加载 32 位! | 0.1.1 |   |
| feat: 加载 Stage2 成功, 目前 16 位 | 0.1.0 |   |
