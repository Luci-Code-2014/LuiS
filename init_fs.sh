#!/bin/bash

# 确保脚本遇到错误立刻停止
set -e

# 提示
echo "注: 本程序需要 sudo 权限是正常行为, 因为程序需要创建 /mnt/ 下的文件夹, 可以查看文件内容"

# 1. 定义变量
IMG_FILE="fs/rootfs.img"
MOUNT_DIR="/mnt/luis"
IMG_SIZE="64M"
SIZE_MB=${IMG_SIZE%M}

echo "===== 准备生成 rootfs 镜像 ====="

# 2. 创建空白的磁盘镜像文件
if [ -f "$IMG_FILE" ]; then
    echo "==> 警告: $IMG_FILE 已存在, 正在覆盖..."
    rm -f "$IMG_FILE"
fi
# 用 dd 命令生成一个全是 0 的指定大小的文件
dd if=/dev/zero of="$IMG_FILE" bs=1M count=$SIZE_MB status=none
echo "==> 已创建 $IMG_FILE 的空白镜像"

# 3. 格式化为 ext4 文件系统
mkfs.ext4 -q -F "$IMG_FILE"
echo "==> 格式化 ext4 完成"

# 4. 挂载镜像并写入初始文件
echo "==> 正在挂载并写入测试文件..."
mkdir -p "$MOUNT_DIR"
sudo mount -o loop "$IMG_FILE" "$MOUNT_DIR"

sudo bash -c "echo 'Hello from luis!' > $MOUNT_DIR/hello.txt"
sudo bash -c "echo 'AHCI Read/Write is working!' > $MOUNT_DIR/hello-ahci.txt"

# 5. 卸载镜像
sudo umount "$MOUNT_DIR"
echo "==> 卸载成功!"
