// kernel/gfx.h
#ifndef GFX_H
#define GFX_H

#include <stdint.h>

void gfx_init(void);
void clear_screen(uint32_t color);
void putpixel(uint32_t x, uint32_t y, uint32_t color);
void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

#endif