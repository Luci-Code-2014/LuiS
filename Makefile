# ===== 工具与产物 =====
NASM := nasm
QEMU := qemu-system-x86_64
IMG  := luis.img

all: $(IMG)

# ===== Stage1 ASM -> Binary =====
boot/stage1.bin: boot/stage1.asm
	$(NASM) -f bin $< -o $@

# ===== Stage2 ASM -> Binary =====
boot/stage2.bin: boot/stage2.asm
	$(NASM) -f bin $< -o $@

# ===== 拼接两个 Binary 成镜像再补齐到 1.44 MB =====
$(IMG): boot/stage1.bin boot/stage2.bin
	cat boot/stage1.bin boot/stage2.bin > $(IMG)
	truncate -s 1474560 $(IMG)

# ===== 运行 =====
run: $(IMG)
	$(QEMU) -drive format=raw,file=$(IMG) -nographic

# ===== 调试 =====
debug: $(IMG)
	$(QEMU) -drive format=raw,file=$(IMG) -s -S -nographic

# ===== 清理产物 =====
clean:
	rm -f boot/*.bin $(IMG)

.PHONY: all run debug clean