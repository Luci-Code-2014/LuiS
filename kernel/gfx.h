// kernel/gfx.h
#ifndef GFX_H
#define GFX_H

#include <stdint.h>

void gfx_init(void);
void putpixel(uint32_t x, uint32_t y, uint32_t color);
void clear_screen(uint32_t color);
void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void draw_char(uint32_t x, uint32_t y, char c, uint32_t color);
uint32_t draw_string(uint32_t x, uint32_t y, const char *str, uint32_t color);
void draw_mouse_cursor(int x, int y);

#endif