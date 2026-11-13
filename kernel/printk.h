#ifndef PRINTK_H
#define PRINTK_H

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

void serial_init(void);
void printk(const char *fmt, ...);

#endif