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

**LuiS** 是一个由 Luci 开发的操作系统, 目前团队里只有 1 人

输入 `make run` 运行

## 驱动列表

| 路径 | 介绍 |
| --- | --- |
| kernel/gfx.c | 图形界面 |
| kernel/keyboard.c | PS/2 键盘驱动 |
| kernel/mouse.c | PS/2 鼠标驱动 |
| kernel/ata.c | ATA PIO 硬盘驱动 |
| kernel/ahci.c | AHCI (SATA) 硬盘驱动 |
| kernel/ext4.c | Ext4 文件系统驱动 |
| kernel/blockdev.c | 块设备抽象层 |
| kernel/usb.c | 支持 xHCI 和 eHCI 的 USB 驱动 |
