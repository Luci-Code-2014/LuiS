// kernel/kheap.h
#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>

void kheap_init(void);
void *kmalloc(uint32_t size);
void kfree(void *ptr);

#endif