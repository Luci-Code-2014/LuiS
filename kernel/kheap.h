// kernel/kheap.h
#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>

// 内核堆的管理块头部 (8 字节对齐)
typedef struct block_header {
    uint32_t size;              // 本块的数据区大小 (不含头部)
    uint32_t is_free;           // 1 = 空闲, 0 = 占用
    struct block_header *next;  // 指向下一个物理相邻的块
    struct block_header *prev;  // 指向上一个物理相邻的块 (方便合并)
} block_header_t;

void kheap_init(void);
void *kmalloc(uint32_t size);
void kfree(void *ptr);

#endif