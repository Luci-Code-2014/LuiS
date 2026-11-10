// kernel/ext4.c
#include "ext4.h"
#include "gfx.h"
#include "rootfs_data.h"
#include "ata.h"

void printk(const char *fmt, ...);

static ext4_superblock_t sb;
static uint32_t block_size = 1024;
static uint32_t inode_size = 256;

char ext4_files[16][64];
int ext4_file_count = 0;

/* 静态读取超级块 */
static void read_superblock(uint8_t *buffer) {
    for (uint32_t i = 0; i < sizeof(ext4_superblock_t); i++) {
        buffer[i] = _data_qemu_luis_rootfs_img[1024 + i];
    }
}

/* 统一走 ATA 读块, 盘号 0xF0 表示 -hdb 从盘 LBA 模式 */
static void ata_read_block(uint32_t block_num, uint8_t *buffer) {
    uint32_t sectors = block_size / 512;
    uint32_t lba = block_num * sectors;
    for (uint32_t s = 0; s < sectors; s++) {
        ata_read_sector(0x1F0, 0xF0, lba + s, buffer + s * 512);
    }
}

/* 统一走 ATA 写块 */
static void ata_write_block(uint32_t block_num, uint8_t *buffer) {
    uint32_t sectors = block_size / 512;
    uint32_t lba = block_num * sectors;
    for (uint32_t s = 0; s < sectors; s++) {
        ata_write_sector(0x1F0, 0xF0, lba + s, buffer + s * 512);
    }
}

void ext4_init(void) {
    printk("=== Ext4 Mount & Write ===\n");
    
    // 1. 读超级块
    read_superblock((uint8_t*)&sb);
    if (sb.s_magic != 0xEF53) {
        printk("Bad magic!\n");
        return;
    }
    
    block_size = 1024 << sb.s_log_block_size;
    inode_size = sb.s_inode_size;
    printk("block_size=%d, inode_size=%d\n", block_size, inode_size);

    // 2. 读块组描述符
    uint32_t gdt_block = (block_size == 1024) ? 2 : 1;
    uint8_t *gdt_buffer = (uint8_t*)0x300000;
    ata_read_block(gdt_block, gdt_buffer);
    
    ext4_group_desc_t *gdt = (ext4_group_desc_t*)gdt_buffer;
    uint32_t inode_table_block = gdt[0].bg_inode_table_lo;

    // 3. 定位根目录 Inode (inode 2)
    uint32_t root_inode_offset = (2 - 1) * inode_size;
    uint32_t root_inode_block = inode_table_block + (root_inode_offset / block_size);
    uint32_t root_inode_off = root_inode_offset % block_size;

    uint8_t *inode_buffer = (uint8_t*)0x400000;
    ata_read_block(root_inode_block, inode_buffer);
    ext4_inode_t *root_inode = (ext4_inode_t*)(inode_buffer + root_inode_off);

    // 4. 解析根目录的数据块
    uint8_t *iblock = (uint8_t *)root_inode->i_block;
    uint32_t ee_start_hi = *(uint16_t*)(iblock + 12 + 6);
    uint32_t ee_start_lo = *(uint32_t*)(iblock + 12 + 8);
    uint32_t root_data_block = (ee_start_hi << 16) | ee_start_lo;

    uint8_t *dir_buffer = (uint8_t*)0x500000;
    ata_read_block(root_data_block, dir_buffer);

    // 5. 遍历目录项找 hello.txt
    uint32_t offset = 0;
    ext4_file_count = 0;
    uint32_t hello_inode_num = 0;

    while (offset < block_size) {
        ext4_dir_entry_t *entry = (ext4_dir_entry_t*)(dir_buffer + offset);
        if (entry->rec_len < 8) break;

        if (entry->inode != 0 && entry->name_len > 0) {
            char name[64];
            for (int i = 0; i < entry->name_len; i++) name[i] = entry->name[i];
            name[entry->name_len] = '\0';
            
            if (ext4_file_count < 16) {
                for(int i=0; i<=entry->name_len; i++) ext4_files[ext4_file_count][i] = name[i];
                ext4_file_count++;
            }

            if (entry->name_len == 9 && name[0] == 'h' && name[1] == 'e') {
                hello_inode_num = entry->inode;
            }
        }
        offset += entry->rec_len;
    }

    if (hello_inode_num != 0) {
        printk("Found hello.txt, inode: %d\n", hello_inode_num);
        
        // 读取 hello.txt 的 Inode
        uint32_t file_inode_offset = (hello_inode_num - 1) * inode_size;
        uint32_t file_inode_block = inode_table_block + (file_inode_offset / block_size);
        uint32_t file_inode_off = file_inode_offset % block_size;

        ata_read_block(file_inode_block, inode_buffer);
        ext4_inode_t *file_inode = (ext4_inode_t*)(inode_buffer + file_inode_off);

        // 找数据块
        uint8_t *fiblock = (uint8_t *)file_inode->i_block;
        uint32_t f_hi = *(uint16_t*)(fiblock + 12 + 6);
        uint32_t f_lo = *(uint32_t*)(fiblock + 12 + 8);
        uint32_t file_data_block = (f_hi << 16) | f_lo;

        printk("File data block: %d\n", file_data_block);

        // 构造新内容
        uint8_t *write_buffer = (uint8_t*)0x600000;
        for (uint32_t i = 0; i < block_size; i++) write_buffer[i] = 0;
        
        const char *new_content = "Hello from LuiS OS (Overwritten!)\n";
        int len = 0;
        while (new_content[len]) { write_buffer[len] = new_content[len]; len++; }

        // 写入数据块
        ata_write_block(file_data_block, write_buffer);
        
        // 立刻从 ATA 磁盘读回
        uint8_t *verify_buf = (uint8_t*)0x800000;
        ata_read_block(file_data_block, verify_buf);
        
        printk("Read back from disk: ");
        for (int i = 0; i < len; i++) {
            printk("%c", verify_buf[i]); // 直接看字符输出
        }
        printk("\n");

        // 更新 Inode 里的文件大小
        uint8_t *inode_write_buf = (uint8_t*)0x700000;
        ata_read_block(file_inode_block, inode_write_buf); 
        ext4_inode_t *inode_to_update = (ext4_inode_t*)(inode_write_buf + file_inode_off);
        
        inode_to_update->i_size_lo = len;
        *(uint32_t*)((uint8_t*)inode_to_update + 108) = 0;
        inode_to_update->i_blocks_lo = (len + 511) / 512;

        // 写入 Inode
        ata_write_block(file_inode_block, inode_write_buf);
        printk("Inode updated!\n");
    }
}