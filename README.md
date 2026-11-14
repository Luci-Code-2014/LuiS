# LuiS

## 介绍

**LuiS** 是一个 **操作系统**

全称是 **Luci_Code-Operating-System**

目前仅支持 **Legacy BIOS**

支持图形化

使用 `ext4` 文件系统

## 运行

使用 `make run` 运行测试

系统要求: Linux

编译要求: qemu-system-x86_64, nasm, gdb, gcc, ld 等基础包

> 开发者: LuiS

## 介绍

输入 `make run` 运行

## 驱动列表

| 路径 | 介绍 |
| --- | --- |
| kernel/usb_hid.c | USB HID 外设协议栈解析 |
| kernel/usb_hid_ehci.c | USB HID 在 EHCI 下的端点传输层 |
| kernel/ehci.c | USB 2.0 (EHCI) 底层控制器驱动
| kernel/xhci.c | USB 3.0 (xHCI) 底层控制器驱动 |
| kernel/gfx.c | 图形界面 |
| kernel/keyboard.c | PS/2 键盘驱动 |
| kernel/mouse.c | PS/2 鼠标驱动 |
| kernel/ata.c | ATA PIO 硬盘驱动 |
| kernel/ahci.c | AHCI (SATA) 硬盘驱动 |
| kernel/ext4.c | Ext4 文件系统驱动 |
| kernel/blockdev.c | 块设备抽象层 |
| kernel/usb.c | USB 核心分发层 |
