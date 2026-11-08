// kernel/pmm.c
#include "pmm.h"

// 最多管理 4GB 内存 (4GB / 4KB / 8bit = 128KB 的位图大小)
static uint32_t bitmap[32768]; 
static uint32_t total_pages = 0;
static uint32_t used_pages = 0;

// 设置某个位 (标记占用)
static void bitmap_set(uint32_t page_idx) {
    bitmap[page_idx / 32] |= (1 << (page_idx % 32));
}

// 清除某个位 (标记空闲)
static void bitmap_clear(uint32_t page_idx) {
    bitmap[page_idx / 32] &= ~(1 << (page_idx % 32));
}

// 检查某个位是否被占用
static int bitmap_test(uint32_t page_idx) {
    return (bitmap[page_idx / 32] & (1 << (page_idx % 32))) != 0;
}

void pmm_init(uint32_t total_memory_size) {
    total_pages = total_memory_size / PAGE_SIZE;
    
    // 把所有内存先标记为全部占用 (1)
    for (uint32_t i = 0; i < 32768; i++) {
        bitmap[i] = 0xFFFFFFFF;
    }
    
    // 然后把可用的内存区域标记为空闲 (0)
    // 因为你的内核加载在 0x10000，为了绝对安全, 我们把前 4MB 内存全部保留给内核和显存
    uint32_t reserved_pages = 0x400000 / PAGE_SIZE; // 4MB / 4KB = 1024 个页
    
    for (uint32_t i = reserved_pages; i < total_pages; i++) {
        bitmap_clear(i);
    }
    
    used_pages = reserved_pages;
}

void *pmm_alloc_page(void) {
    // 从保留区之后开始找空闲页
    for (uint32_t i = 0x400000 / PAGE_SIZE; i < total_pages; i++) {
        if (!bitmap_test(i)) { // 如果是 0 (空闲)
            bitmap_set(i);     // 标记为 1 (占用)
            used_pages++;
            // 返回这块物理页的绝对地址
            return (void *)(unsigned long)(i * PAGE_SIZE);
        }
    }
    return 0; // 内存耗尽
}

void pmm_free_page(void *ptr) {
    unsigned long addr = (unsigned long)ptr;
    // 检查地址是否对齐且合法
    if (addr % PAGE_SIZE != 0 || addr >= total_pages * PAGE_SIZE) return;
    
    uint32_t page_idx = addr / PAGE_SIZE;
    if (bitmap_test(page_idx)) {
        bitmap_clear(page_idx);
        used_pages--;
    }
}