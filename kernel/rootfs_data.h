// kernel/rootfs_data.h
#ifndef ROOTFS_DATA_H
#define ROOTFS_DATA_H

/* fs/rootfs.img 经 `ld -r -b binary fs/rootfs.img` 生成的符号 */
extern const unsigned char _binary_fs_rootfs_img_start[];
extern const unsigned char _binary_fs_rootfs_img_end[];
extern const unsigned char _binary_fs_rootfs_img_size[];

/* ext4.c 里使用的名字保持不变 */
#define _data_qemu_luis_rootfs_img _binary_fs_rootfs_img_start

#endif