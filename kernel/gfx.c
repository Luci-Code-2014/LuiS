// kernel/gfx.c
#include "gfx.h"
#include "font8x16.h"

static uint8_t *vram = 0;
static uint32_t fb_width = 1024;   // 下面会从 VBE 动态获取
static uint32_t fb_height = 768;
static uint32_t fb_pitch = 3072; 
static uint32_t fb_bpp = 24;       // 24 或 32

void gfx_init(void) {
    uint32_t fb_addr = *(volatile uint32_t *)0x5000;
    uint32_t pitch   = *(volatile uint32_t *)0x5004;
    uint32_t bpp     = *(volatile uint32_t *)0x5008;
    vram = (uint8_t *)(unsigned long)fb_addr;
    fb_pitch = pitch;
    fb_bpp = bpp;

    if (fb_pitch == 5760 || fb_pitch == 7680) {
        fb_width = 1920;
        fb_height = 1080;
    } else {
        fb_width = 1024;
        fb_height = 768;
    }
}

void putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!vram || x >= fb_width || y >= fb_height) return;

    uint32_t offset = y * fb_pitch + x * (fb_bpp / 8);
    
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8)  & 0xFF;
    uint8_t b = color         & 0xFF;

    vram[offset + 0] = b;
    vram[offset + 1] = g;
    vram[offset + 2] = r;
    if (fb_bpp == 32) vram[offset + 3] = 0;
}

void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            putpixel(x + i, y + j, color);
        }
    }
}

void clear_screen(uint32_t color) {
    fill_rect(0, 0, fb_width, fb_height, color);
}

void draw_char(uint32_t x, uint32_t y, char ch, uint32_t color) {
    unsigned char c = (unsigned char)ch;
    if (c < 32 || c > 126) return;
    uint8_t *glyph = (uint8_t *)font8x16[c - 32];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                putpixel(x + col, y + row, color);
            }
        }
    }
}

uint32_t draw_string(uint32_t x, uint32_t y, const char *str, uint32_t color) {
    uint32_t cur_x = x, cur_y = y;
    while (*str) {
        char c = *str;
        if (c == '\n') {
            cur_x = x;
            cur_y += 16;
        } else {
            draw_char(cur_x, cur_y, c, color);
            cur_x += 8;
            if (cur_x + 8 > fb_width) {
                cur_x = x;
                cur_y += 16;
            }
        }
        str++;
    }
    return cur_y + 16;
}

void draw_mouse_cursor(int x, int y) {
    // 经典 12x19 Windows 鼠标指针 (0 = 透明，1 = 白，2 = 黑)
    static const uint8_t cursor[19][12] = {
        {2,0,0,0,0,0,0,0,0,0,0,0},
        {2,2,0,0,0,0,0,0,0,0,0,0},
        {2,1,2,0,0,0,0,0,0,0,0,0},
        {2,1,1,2,0,0,0,0,0,0,0,0},
        {2,1,1,1,2,0,0,0,0,0,0,0},
        {2,1,1,1,1,2,0,0,0,0,0,0},
        {2,1,1,1,1,1,2,0,0,0,0,0},
        {2,1,1,1,1,1,1,2,0,0,0,0},
        {2,1,1,1,1,1,1,1,2,0,0,0},
        {2,1,1,1,1,1,1,1,1,2,0,0},
        {2,1,1,1,1,1,1,1,1,1,2,0},
        {2,1,1,1,1,1,1,2,2,2,2,2},
        {2,1,1,1,2,1,1,2,0,0,0,0},
        {2,1,1,2,0,2,1,1,2,0,0,0},
        {2,1,2,0,0,2,1,1,2,0,0,0},
        {2,2,0,0,0,0,2,1,1,2,0,0},
        {0,0,0,0,0,0,2,1,1,2,0,0},
        {0,0,0,0,0,0,0,2,1,1,2,0},
        {0,0,0,0,0,0,0,2,2,2,0,0}
    };

    for (int row = 0; row < 19; row++) {
        for (int col = 0; col < 12; col++) {
            uint8_t pixel = cursor[row][col];
            if (pixel == 1) {
                putpixel(x + col, y + row, 0x00FFFFFF); // 内部纯白
            } else if (pixel == 2) {
                putpixel(x + col, y + row, 0x00000000); // 黑色描边
            }
        }
    }
}