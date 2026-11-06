// kernel/gfx.c
#include "gfx.h"

static uint8_t *fb = 0;
static uint32_t fb_width = 1024;
static uint32_t fb_height = 768;
static uint32_t fb_pitch = 3072; 

void gfx_init(void) {
    uint32_t fb_addr = *(volatile uint32_t *)0x5000;
    uint32_t pitch   = *(volatile uint32_t *)0x5004;
    
    fb = (uint8_t *)(unsigned long)fb_addr;
    fb_pitch = pitch;
}

void putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb || x >= fb_width || y >= fb_height) return;

    uint32_t offset = y * fb_pitch + x * 3;
    
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8)  & 0xFF;
    uint8_t b = color         & 0xFF;

    fb[offset + 0] = b;
    fb[offset + 1] = g;
    fb[offset + 2] = r;
}

void clear_screen(uint32_t color) {
    for (uint32_t y = 0; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            putpixel(x, y, color);
        }
    }
}

void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            putpixel(x + i, y + j, color);
        }
    }
}