// kernel/ext4.c
#include "ext4.h"
#include "gfx.h"
#include "blockdev.h"
#include "printk.h"

static ext4_superblock_t sb;
static uint32_t block_size = 1024;
static uint32_t inode_size = 256;

char ext4_files[16][64];
int ext4_file_count = 0;

static void ata_read_block(uint32_t block_num, uint8_t *buffer) {
    uint32_t sectors = block_size / 512;
    uint64_t lba = (uint64_t)block_num * sectors;
    for (uint32_t s = 0; s < sectors; s++) {
        if (blockdev_read_sector(lba + s, buffer + s * 512) != 0) {
            printk("%[E] ext4: 读块失败 block=%d\n", block_num);
            return;
        }
    }
}

static void ata_write_block(uint32_t block_num, uint8_t *buffer) {
    if (!blockdev_can_write()) return;
    uint32_t sectors = block_size / 512;
    uint64_t lba = (uint64_t)block_num * sectors;
    for (uint32_t s = 0; s < sectors; s++) {
        blockdev_write_sector(lba + s, buffer + s * 512);
    }
}

static void parse_extent_tree(uint8_t *node, uint32_t *blocks, uint32_t *count, int max_blocks) {
    uint16_t entries = *(uint16_t*)(node + 2);
    uint16_t depth = *(uint16_t*)(node + 6);

    if (depth == 0) {
        for (int i = 0; i < entries && *count < (uint32_t)max_blocks; i++) {
            uint8_t *ext = node + 12 + i * 12;
            uint32_t ee_start_hi = *(uint16_t*)(ext + 6);
            uint32_t ee_start_lo = *(uint32_t*)(ext + 8);
            blocks[(*count)++] = (ee_start_hi << 16) | ee_start_lo;
        }
    } else {
        for (int i = 0; i < entries && *count < (uint32_t)max_blocks; i++) {
            uint8_t *ext = node + 12 + i * 12;
            uint32_t leaf_hi = *(uint16_t*)(ext + 6);
            uint32_t leaf_lo = *(uint32_t*)(ext + 8);
            uint32_t leaf_block = (leaf_hi << 16) | leaf_lo;

            uint8_t *tmp = (uint8_t*)0x900000;
            ata_read_block(leaf_block, tmp);
            parse_extent_tree(tmp, blocks, count, max_blocks);
        }
    }
}

static int get_inode_data_blocks(uint32_t inode_num, uint32_t inode_table_block, uint32_t *blocks, uint32_t *count, int max_blocks) {
    uint32_t inode_offset = (inode_num - 1) * inode_size;
    uint32_t inode_block = inode_table_block + (inode_offset / block_size);
    uint32_t inode_off = inode_offset % block_size;

    uint8_t *inode_buffer = (uint8_t*)0x400000;
    ata_read_block(inode_block, inode_buffer);
    uint8_t *inode_raw = inode_buffer + inode_off;

    uint32_t i_flags = *(uint32_t*)(inode_raw + 32);
    printk("%[I] ext4: Inode %d 标志=0x%x, 大小=%d\n", inode_num, i_flags, *(uint32_t*)(inode_raw + 4));

    if (i_flags & 0x80000) {
        parse_extent_tree(inode_raw + 40, blocks, count, max_blocks);
        return 0;
    } else {
        for (int i = 0; i < 12 && *count < (uint32_t)max_blocks; i++) {
            uint32_t blk = *(uint32_t*)(inode_raw + 40 + i * 4);
            if (blk != 0) blocks[(*count)++] = blk;
        }
        return 0;
    }
}

void ext4_init(void) {
    blockdev_type_t dev_type = blockdev_init();
    if (dev_type == BLOCKDEV_NONE) {
        printk("%[E] ext4: 无块设备\n");
        return;
    }

    printk("%[I] === Ext4 挂载与写入 ===\n");

    const char *target_name = "hello.txt";
    const char *write_content = "Hello from LuiS OS (ATA Overwritten!)\n";
    uint8_t target_len = 9;

    if (dev_type == BLOCKDEV_AHCI) {
        target_name = "hello-ahci.txt";
        write_content = "Written via AHCI successfully!\n";
        target_len = 14;
        printk("%[I] ext4: [AHCI 模式] 锁定目标文件: %s\n", target_name);
    } else if (dev_type == BLOCKDEV_ATA) {
        printk("%[I] ext4: [ATA 模式] 锁定目标文件: %s\n", target_name);
    }

    // 使用你 dumpe2fs 里的固定参数: block_size=1024, inode 表起始块=68
    block_size = 1024;
    inode_size = 256;
    uint32_t inode_table_block = 68;
    printk("%[I] ext4: 使用参数 block_size=%d, inode 表起始块=%d\n", block_size, inode_table_block);

    // 1. 获取根目录 (Inode 2) 的数据块
    uint32_t root_data_blocks[32] = {0};
    uint32_t root_block_count = 0;
    get_inode_data_blocks(2, inode_table_block, root_data_blocks, &root_block_count, 32);

    if (root_block_count == 0) {
        printk("%[E] ext4: 根目录没有数据块\n");
        return;
    }

    // 2. 遍历根目录找文件
    uint8_t *dir_buffer = (uint8_t*)0x500000;
    ext4_file_count = 0;
    uint32_t target_inode_num = 0;

    for (uint32_t b = 0; b < root_block_count; b++) {
        ata_read_block(root_data_blocks[b], dir_buffer);
        uint32_t offset = 0;

        while (offset < block_size) {
            ext4_dir_entry_t *entry = (ext4_dir_entry_t*)(dir_buffer + offset);
            if (entry->rec_len < 8) break;

            if (entry->inode != 0 && entry->name_len > 0) {
                char name[64];
                for (int i = 0; i < entry->name_len; i++) name[i] = entry->name[i];
                name[entry->name_len] = '\0';

                printk("%[I] ext4: 遍历到文件: %s (inode=%d)\n", name, entry->inode);

                if (ext4_file_count < 16) {
                    for (int i = 0; i <= entry->name_len; i++) ext4_files[ext4_file_count][i] = name[i];
                    ext4_file_count++;
                }

                if (entry->name_len == target_len) {
                    int match = 1;
                    for (int i = 0; i < target_len; i++) {
                        if (name[i] != target_name[i]) { match = 0; break; }
                    }
                    if (match) {
                        target_inode_num = entry->inode;
                        printk("%[O] ext4: 匹配到目标文件! inode=%d\n", target_inode_num);
                    }
                }
            }
            offset += entry->rec_len;
        }
    }

    // 3. 动态解析目标文件的数据块并读写
    if (target_inode_num != 0 && dev_type != BLOCKDEV_MEM) {
        uint32_t file_data_blocks[32] = {0};
        uint32_t file_block_count = 0;

        // 动态解析文件所在的 Extent 树, 找到真正的数据块
        get_inode_data_blocks(target_inode_num, inode_table_block, file_data_blocks, &file_block_count, 32);

        if (file_block_count > 0) {
            uint32_t file_data_block = file_data_blocks[0];
            printk("%[I] ext4: 动态解析出目标文件物理数据块: %d\n", file_data_block);

            uint8_t *write_buffer = (uint8_t*)0x600000;
            for (uint32_t i = 0; i < block_size; i++) write_buffer[i] = 0;

            int len = 0;
            while (write_content[len]) {
                write_buffer[len] = write_content[len];
                len++;
            }

            ata_write_block(file_data_block, write_buffer);

            uint8_t *verify_buf = (uint8_t*)0x800000;
            ata_read_block(file_data_block, verify_buf);

            printk("%[I] ext4: 磁盘读回: ");
            for (int i = 0; i < len; i++) {
                printk("%c", verify_buf[i]);
            }
            printk("\n");

            uint32_t inode_offset = (target_inode_num - 1) * inode_size;
            uint32_t inode_block = inode_table_block + (inode_offset / block_size);
            uint32_t inode_off = inode_offset % block_size;

            uint8_t *inode_update_buf = (uint8_t*)0x700000;
            ata_read_block(inode_block, inode_update_buf);
            uint8_t *inode_raw = inode_update_buf + inode_off;

            *(uint32_t*)(inode_raw + 4) = len;
            *(uint32_t*)(inode_raw + 28) = (len + 511) / 512;

            ata_write_block(inode_block, inode_update_buf);
            printk("%[O] ext4: Inode 已更新 (大小=%d, 块数=%d)\n", len, (len + 511) / 512);
        } else {
            printk("%[W] ext4: 无法获取目标文件数据块，跳过读写\n");
        }
    }
}
