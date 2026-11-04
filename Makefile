# ===== 工具与产物 =====
NASM  := nasm
CC    := gcc
LD    := ld
OBJCOPY := objcopy
QEMU  := qemu-system-x86_64
IMG   := luis.img

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

kernel/idt.o: kernel/idt.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/kernel.o: kernel/kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

# ===== 链接 =====
kernel/kernel.elf: kernel/entry/kernel_entry.o kernel/isr.o kernel/idt.o kernel/kernel.o kernel/kernel.ld
	$(LD) -T kernel/kernel.ld -o $@ \
		kernel/entry/kernel_entry.o kernel/isr.o kernel/idt.o kernel/kernel.o

kernel/kernel.bin: kernel/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@echo "kernel.bin size: $$(stat -c%s $@) bytes"

# ===== 镜像 =====
$(IMG): boot/stage1.bin boot/stage2.bin kernel/kernel.bin
	cat boot/stage1.bin boot/stage2.bin kernel/kernel.bin > $(IMG)
	truncate -s 1474560 $(IMG)

# ===== 运行 =====
run: $(IMG)
	$(QEMU) -drive format=raw,file=$(IMG) -nographic -no-reboot

debug: $(IMG)
	$(QEMU) -drive format=raw,file=$(IMG) -s -S -nographic -no-reboot

# ===== 清理 =====
clean:
	rm -f boot/*.bin
	rm -f kernel/*.o kernel/*.elf kernel/*.bin
	rm -f kernel/entry/*.o
	rm -f $(IMG)

.PHONY: all run debug clean