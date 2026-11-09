// kernel/ext4.c
#include "ext4.h"
#include "gfx.h"
#include "rootfs_data.h"

void printk(const char *fmt, ...);

static ext4_superblock_t sb;
static uint32_t block_size = 1024;
static uint32_t inode_size = 128;

char ext4_files[16][64];
int ext4_file_count = 0;

static void read_fs_block(uint32_t block_num, uint8_t *buffer) {
    uint32_t offset = block_num * block_size;
    if (offset + block_size > 65536) {
        for (uint32_t i = 0; i < block_size; i++) buffer[i] = 0;
        return;
    }
    for (uint32_t i = 0; i < block_size; i++) {
        buffer[i] = _data_qemu_luis_rootfs_img[offset + i];
    }
}

void ext4_init(void) {
    printk("=== Ext4 Debug Start ===\n");
    
    for (uint32_t i = 0; i < sizeof(ext4_superblock_t); i++) {
        ((uint8_t*)&sb)[i] = _data_qemu_luis_rootfs_img[1024 + i];
    }

    if (sb.s_magic != 0xEF53) {
        printk("Ext4: Bad Magic! Got 0x%x\n", sb.s_magic);
        return;
    }

    block_size = 1024 << sb.s_log_block_size;
    inode_size = sb.s_inode_size;
    printk("Ext4: Magic OK! Block size %d\n", block_size);

    uint32_t gdt_block = (block_size == 1024) ? 2 : 1;
    uint8_t *gdt_buffer = (uint8_t*)0x300000;
    read_fs_block(gdt_block, gdt_buffer);

    ext4_group_desc_t *gdt = (ext4_group_desc_t*)gdt_buffer;
    uint32_t inode_table_block = gdt[0].bg_inode_table_lo;

    uint32_t root_inode_offset = (2 - 1) * inode_size;
    uint32_t root_inode_block = inode_table_block + (root_inode_offset / block_size);
    uint32_t root_inode_off_in_block = root_inode_offset % block_size;

    uint8_t *inode_buffer = (uint8_t*)0x400000;
    read_fs_block(root_inode_block, inode_buffer);

    ext4_inode_t *root_inode = (ext4_inode_t*)(inode_buffer + root_inode_off_in_block);
    uint8_t *iblock = (uint8_t *)root_inode->i_block;
    uint16_t magic = *(uint16_t*)(iblock);

    if (magic != 0xF30A) {
        printk("Extents magic error: 0x%x\n", magic);
        return;
    }

    uint8_t *extent = iblock + 12; 
    uint32_t ee_start_hi = *(uint16_t*)(extent + 6);
    uint32_t ee_start_lo = *(uint32_t*)(extent + 8);
    uint32_t root_data_block = (ee_start_hi << 16) | ee_start_lo;

    uint8_t *dir_buffer = (uint8_t*)0x500000;
    read_fs_block(root_data_block, dir_buffer);

    uint32_t offset = 0;
    uint32_t safety_count = 0;
    ext4_file_count = 0;

    while (offset < block_size && safety_count < 100) {
        safety_count++;
        ext4_dir_entry_t *entry = (ext4_dir_entry_t*)(dir_buffer + offset);
        
        if (entry->rec_len < 8 || entry->rec_len > block_size) break;

        if (entry->inode != 0 && entry->name_len > 0 && entry->name_len < 64) {
            char name_buf[64];
            for (int i = 0; i < entry->name_len; i++) name_buf[i] = entry->name[i];
            name_buf[entry->name_len] = '\0';
            
            if (ext4_file_count < 16) {
                for(int i = 0; i <= entry->name_len; i++) {
                    ext4_files[ext4_file_count][i] = name_buf[i];
                }
                ext4_file_count++;
            }
        }
        offset += entry->rec_len;
    }
    printk("=== Ext4 Debug End ===\n");
}