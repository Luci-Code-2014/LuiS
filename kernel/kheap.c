// kernel/kheap.c
#include "kheap.h"
#include "pmm.h"
#include "printk.h"

#define HEADER_SIZE sizeof(block_header_t) // 通常为 24 字节
#define PAGE_SIZE 4096

static block_header_t *head = 0;

// 在空闲块中寻找足够大的块 (首次适应算法)
static block_header_t *find_free_block(uint32_t size) {
    block_header_t *curr = head;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            return curr;
        }
        curr = curr->next;
    }
    return 0;
}

void kheap_init(void) {
    // 申请第一个 4KB 物理页作为初始堆
    void *page = pmm_alloc_page();
    printk("Kheap Init Page Addr: %p\n", page);
    if (!page) return;

    head = (block_header_t *)page;
    head->size = PAGE_SIZE - HEADER_SIZE;
    head->is_free = 1;
    head->next = 0;
    head->prev = 0;
}

void *kmalloc(uint32_t size) {
    if (size == 0) return 0;

    // 数据区大小按 8 字节对齐
    size = (size + 7) & ~7;

    block_header_t *block = find_free_block(size);
    
    // 如果没找到空闲块, 再向 PMM 申请一整页, 挂到链表尾部
    if (!block) {
        void *new_page = pmm_alloc_page();
        if (!new_page) return 0;

        block_header_t *new_block = (block_header_t *)new_page;
        new_block->size = PAGE_SIZE - HEADER_SIZE;
        new_block->is_free = 1;
        new_block->next = 0;

        // 找到当前链表尾部
        block_header_t *curr = head;
        while (curr->next) curr = curr->next;
        
        curr->next = new_block;
        new_block->prev = curr;

        block = new_block;
    }

    // 如果这个块足够大, 我们把它劈开 (分裂)
    // 剩下的空间至少能容纳一个头部 + 8 字节的数据, 才有劈开的意义
    if (block->size >= size + HEADER_SIZE + 8) {
        block_header_t *new_block = (block_header_t *)((uint8_t *)block + HEADER_SIZE + size);
        new_block->size = block->size - size - HEADER_SIZE;
        new_block->is_free = 1;
        new_block->next = block->next;
        new_block->prev = block;
        
        if (block->next) {
            block->next->prev = new_block;
        }
        
        block->size = size;
        block->next = new_block;
    }

    block->is_free = 0;
    printk("KMALLOC returning: %p\n", (void *)((uint8_t *)block + HEADER_SIZE));
    return (void *)((uint8_t *)block + HEADER_SIZE);
    return (void *)((uint8_t *)block + HEADER_SIZE);
}

void kfree(void *ptr) {
    if (!ptr) return;

    // 反推头部
    block_header_t *block = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);
    block->is_free = 1;

    // ===== 内存合并 (Coalescing) 核心逻辑 =====

    // 1. 向后合并: 如果下一个块也是空闲的, 合并
    if (block->next && block->next->is_free) {
        block->size += block->next->size + HEADER_SIZE;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }

    // 2. 向前合并: 如果上一个块也是空闲的, 把当前块融合进上一个块
    if (block->prev && block->prev->is_free) {
        block->prev->size += block->size + HEADER_SIZE;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
    }
}