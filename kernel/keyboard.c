// kernel/keyboard.c
#include "keyboard.h"

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "d"(port));
    return ret;
}

/* Set 1 扫描码 -> ASCII (未按 Shift) */
static const char scancode_ascii[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t','q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' ', 0, /* ... 其余 F1~F12 等为 0 */
};

static int shift_down = 0;

static char apply_shift(char c) {
    if (!shift_down) return c;
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    if (c == '1') return '!';
    if (c == '2') return '@';
    if (c == '3') return '#';
    if (c == '4') return '$';
    if (c == '5') return '%';
    if (c == '6') return '^';
    if (c == '7') return '&';
    if (c == '8') return '*';
    if (c == '9') return '(';
    if (c == '0') return ')';
    if (c == '-') return '_';
    if (c == '=') return '+';
    if (c == '[') return '{';
    if (c == ']') return '}';
    if (c == ';') return ':';
    if (c == '\'') return '"';
    if (c == ',') return '<';
    if (c == '.') return '>';
    if (c == '/') return '?';
    if (c == '\\') return '|';
    if (c == '`') return '~';
    return c;
}

void keyboard_handle_irq(void) {
    unsigned char sc = inb(0x60);   // 读扫描码

    if (sc == 0x2A || sc == 0x36) { shift_down = 1; return; }   // Shift 按下
    if (sc == 0xAA || sc == 0xB6) { shift_down = 0; return; }   // Shift 松开
    if (sc & 0x80) return;          // 其他键的松开事件, 忽略

    char c = scancode_ascii[sc];
    if (c) {
        char out[2] = { apply_shift(c), 0 };
        extern void printk(const char *, ...);
        printk("%s", out);
    }
}