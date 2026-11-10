# ===== 工具与产物 =====
NASM  := nasm
CC    := gcc
LD    := ld
OBJCOPY := objcopy
QEMU  := qemu-system-x86_64

IMG        := luis.img
ROOTFS_IMG := fs/rootfs.img

all: $(IMG)

# ===== Stage1 / Stage2 =====
boot/stage1.bin: boot/stage1.asm
	$(NASM) -f bin $< -o $@

boot/stage2.bin: boot/stage2.asm
	$(NASM) -f bin $< -o $@

# ===== Kernel 编译选项 =====
CFLAGS := -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
    -mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
    -mcmodel=small -Wall -Wextra -O2 \
    -I kernel

# ===== Kernel 目标文件 =====
kernel/entry/kernel_entry.o: kernel/entry/kernel_entry.asm
	$(NASM) -f elf64 $< -o $@

kernel/isr.o: kernel/isr.asm
	$(NASM) -f elf64 $< -o $@

kernel/idt.o: kernel/idt.c kernel/idt.h kernel/isr.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/pic.o: kernel/pic.c kernel/pic.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/pit.o: kernel/pit.c kernel/pit.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/gfx.o: kernel/gfx.c kernel/gfx.h kernel/font8x16.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/keyboard.o: kernel/keyboard.c kernel/keyboard.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/mouse.o: kernel/mouse.c kernel/mouse.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/kernel.o: kernel/kernel.c kernel/idt.h kernel/pic.h kernel/pit.h \
    kernel/keyboard.h kernel/gfx.h kernel/mouse.h kernel/ext4.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/panic.o: kernel/panic.c kernel/panic.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/pmm.o: kernel/pmm.c kernel/pmm.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/kheap.o: kernel/kheap.c kernel/kheap.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/ata.o: kernel/ata.c kernel/ata.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/ext4.o: kernel/ext4.c kernel/ext4.h kernel/ata.h kernel/rootfs_data.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel/rootfs.o: $(ROOTFS_IMG)
	$(LD) -r -b binary $< -o $@

# ===== 链接 =====
KERNEL_OBJS := kernel/entry/kernel_entry.o \
    kernel/isr.o \
    kernel/idt.o \
    kernel/pic.o \
    kernel/pit.o \
    kernel/gfx.o \
    kernel/keyboard.o \
    kernel/mouse.o \
    kernel/pmm.o \
    kernel/kheap.o \
    kernel/panic.o \
    kernel/ata.o \
    kernel/ext4.o \
    kernel/rootfs.o \
    kernel/kernel.o

kernel/kernel.elf: $(KERNEL_OBJS) kernel/kernel.ld
	$(LD) -T kernel/kernel.ld -o $@ $(KERNEL_OBJS)

kernel/kernel.bin: kernel/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@echo "kernel.bin size: $$(stat -c%s $@) bytes"

# ===== 镜像 =====
$(IMG): boot/stage1.bin boot/stage2.bin kernel/kernel.bin
	cat boot/stage1.bin boot/stage2.bin kernel/kernel.bin > $(IMG)
	@SIZE=$$(stat -c%s $(IMG)); \
	if [ $$SIZE -lt 2097152 ]; then truncate -s 2097152 $(IMG); fi

# ===== 运行 =====
run: $(IMG)
	$(QEMU) -M pc -m 128 -hda $(IMG) -hdb $(ROOTFS_IMG) \
	-serial stdio -display sdl -no-reboot -vga std

debug: $(IMG)
	$(QEMU) -drive format=raw,file=$(IMG) \
	-drive format=raw,file=$(ROOTFS_IMG) \
	-s -S -nographic -no-reboot

# ===== 清理 =====
clean:
	rm -f boot/*.bin
	rm -f kernel/*.o kernel/*.elf kernel/*.bin
	rm -f kernel/entry/*.o
	rm -f $(IMG)

.PHONY: all run debug clean