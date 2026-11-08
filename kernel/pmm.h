// kernel/pmm.h
#ifndef PMM_H
#define PMM_H

#include <stdint.h>

#define PAGE_SIZE 4096

// 初始化物理内存管理器 (传给它可用的物理内存总大小)
void pmm_init(uint32_t total_memory_size);

// 分配一个 4KB 物理页, 返回物理地址, 失败返回 0
void *pmm_alloc_page(void);

// 释放一个 4KB 物理页
void pmm_free_page(void *ptr);

#endif