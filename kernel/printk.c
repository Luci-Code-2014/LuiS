#include "printk.h"

/* ===== 端口 I/O ===== */
static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "d"(port));
}
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "d"(port));
    return ret;
}

void serial_init(void) {
    outb(0x3F8 + 1, 0x00); outb(0x3F8 + 3, 0x80); outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00); outb(0x3F8 + 3, 0x03); outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

static void serial_putc(char c) {
    for (volatile int i = 0; i < 100000; i++) {
        if (inb(0x3FD) & 0x20) break;
    }
    outb(0x3F8, (unsigned char)c);
}

/* ===== printk 基础字符输出 ===== */
static void printk_putc(char c) {
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
}

static void printk_puts(const char *s) {
    if (!s) s = "(null)";
    while (*s) printk_putc(*s++);
}

/* 无符号整型转字符串, base 支持 2/8/10/16 */
static void printk_utoa(uint64_t v, unsigned base, int upper, char *buf) {
    static const char lower[] = "0123456789abcdef";
    static const char upper_d[] = "0123456789ABCDEF";
    const char *digits = upper ? upper_d : lower;
    char tmp[72];
    int i = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (v > 0) { tmp[i++] = digits[v % base]; v /= base; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}

/* 有符号整型 (含最小值安全处理) */
static void printk_itoa(int64_t v, unsigned base, int upper, char *buf) {
    if (v < 0) {
        *buf++ = '-';
        printk_utoa((uint64_t)(-(v + 1)) + 1, base, upper, buf);
    } else {
        printk_utoa((uint64_t)v, base, upper, buf);
    }
}

/* 按宽度输出 (pad 为填充字符，left 为左对齐) */
static void printk_pad(const char *s, int width, char pad, int left) {
    int len = 0;
    while (s[len]) len++;
    if (left) {
        printk_puts(s);
        for (int i = len; i < width; i++) printk_putc(pad);
    } else {
        for (int i = len; i < width; i++) printk_putc(pad);
        printk_puts(s);
    }
}

/* ===== printk 主函数 ===== */
void printk(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') { printk_putc(*fmt++); continue; }
        fmt++;  /* 跳过 '%' */

        /* ---- 日志级别 ---- */
        if (*fmt == '[') {
            fmt++;
            char level = *fmt++;
            if (*fmt == ']') fmt++;
            switch (level) {
                case 'E': printk_puts("[ERROR]"); break;
                case 'I': printk_puts("[INFO]");  break;
                case 'O': printk_puts("[ OK ]");  break;
                case 'W': printk_puts("[WARN]");  break;
                default:  printk_puts("[LOG]");   break;
            }
            continue;
        }

        /* ---- 解析标志 ---- */
        int left = 0, plus = 0, space = 0, zero = 0, alt = 0;
        for (;;) {
            if      (*fmt == '-') { left  = 1; fmt++; }
            else if (*fmt == '+') { plus  = 1; fmt++; }
            else if (*fmt == ' ') { space = 1; fmt++; }
            else if (*fmt == '0') { zero  = 1; fmt++; }
            else if (*fmt == '#') { alt   = 1; fmt++; }
            else break;
        }

        /* ---- 宽度 ---- */
        int width = 0;
        if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }

        /* ---- 精度 ---- */
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') { prec = prec * 10 + (*fmt - '0'); fmt++; }
        }

        /* ---- 长度修饰符 ---- */
        enum { LEN_INT, LEN_L, LEN_LL, LEN_Z, LEN_H, LEN_HH, LEN_J, LEN_T } len = LEN_INT;
        for (;;) {
            if      (fmt[0]=='l' && fmt[1]=='l') { len = LEN_LL; fmt += 2; }
            else if (fmt[0]=='l')                { len = LEN_L;  fmt++;   }
            else if (fmt[0]=='z')                { len = LEN_Z;  fmt++;   }
            else if (fmt[0]=='h' && fmt[1]=='h') { len = LEN_HH; fmt += 2; }
            else if (fmt[0]=='h')                { len = LEN_H;  fmt++;   }
            else if (fmt[0]=='j')                { len = LEN_J;  fmt++;   }
            else if (fmt[0]=='t')                { len = LEN_T;  fmt++;   }
            else break;
        }

        char c = *fmt;
        char buf[80];

        switch (c) {
        case 'c': {
            char ch = (char)va_arg(ap, int);
            buf[0] = ch; buf[1] = 0;
            printk_pad(buf, width, ' ', left);
            fmt++;
            continue;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            if (prec >= 0) {
                int n = 0;
                while (s[n] && n < prec) n++;
                char tmp[256];
                if (n > 255) n = 255;
                for (int i = 0; i < n; i++) tmp[i] = s[i];
                tmp[n] = 0;
                printk_pad(tmp, width, ' ', left);
            } else {
                printk_pad(s, width, ' ', left);
            }
            fmt++;
            continue;
        }
        case '%': {
            printk_putc('%');
            fmt++;
            continue;
        }
        case 'd':
        case 'i': {
            int64_t sv;
            switch (len) {
                case LEN_LL: sv = va_arg(ap, long long); break;
                case LEN_L:  sv = va_arg(ap, long);      break;
                case LEN_Z:  sv = (int64_t)va_arg(ap, size_t); break;
                case LEN_J:  sv = va_arg(ap, intmax_t);  break;
                case LEN_T:  sv = va_arg(ap, ptrdiff_t); break;
                default:     sv = va_arg(ap, int);       break;
            }
            int negative = (sv < 0);
            int has_sign = negative || plus || space;

            char numbuf[72];
            printk_itoa(sv, 10, 0, numbuf);

            const char *numstart = numbuf;
            char signch = 0;
            if (negative) { signch = '-'; numstart = numbuf + 1; }
            else if (plus)  { signch = '+'; }
            else if (space) { signch = ' '; }

            int numlen = 0;
            while (numstart[numlen]) numlen++;
            int total = (has_sign ? 1 : 0) + numlen;
            char pad = (zero && !left) ? '0' : ' ';

            if (left) {
                if (has_sign) printk_putc(signch);
                printk_puts(numstart);
                for (int i = total; i < width; i++) printk_putc(' ');
            } else {
                if (pad == '0' && has_sign) printk_putc(signch);
                for (int i = total; i < width; i++) printk_putc(pad);
                if (!(pad == '0' && has_sign) && has_sign) printk_putc(signch);
                printk_puts(numstart);
            }
            fmt++;
            continue;
        }
        case 'u': case 'o': case 'x': case 'X': case 'b': {
            unsigned base = 10;
            int upper = 0;
            if (c == 'o') base = 8;
            else if (c == 'x') { base = 16; upper = 0; }
            else if (c == 'X') { base = 16; upper = 1; }
            else if (c == 'b') base = 2;

            uint64_t uv;
            switch (len) {
                case LEN_LL: uv = va_arg(ap, unsigned long long); break;
                case LEN_L:  uv = va_arg(ap, unsigned long);      break;
                case LEN_Z:  uv = (uint64_t)va_arg(ap, size_t);   break;
                case LEN_J:  uv = va_arg(ap, uintmax_t);          break;
                case LEN_T:  uv = (uint64_t)va_arg(ap, ptrdiff_t);break;
                case LEN_H:  uv = (unsigned short)va_arg(ap, unsigned int); break;
                case LEN_HH: uv = (unsigned char)va_arg(ap, unsigned int);  break;
                default:     uv = va_arg(ap, unsigned int);       break;
            }

            const char *prefix = "";
            int plen = 0;
            if (alt) {
                if (c == 'x' && uv != 0) { prefix = "0x"; plen = 2; }
                else if (c == 'X' && uv != 0) { prefix = "0X"; plen = 2; }
                else if (c == 'o') { prefix = "0"; plen = 1; }
                else if (c == 'b') { prefix = "0b"; plen = 2; }
            }

            char numbuf[80];
            printk_utoa(uv, base, upper, numbuf);
            int blen = 0;
            while (numbuf[blen]) blen++;

            char padded[80];
            int nlen = 0;
            if (prec > blen) {
                for (int i = 0; i < prec - blen; i++) padded[nlen++] = '0';
            }
            for (int i = 0; i < blen; i++) padded[nlen++] = numbuf[i];
            padded[nlen] = 0;

            int total = plen + nlen;
            char pad = (zero && !left) ? '0' : ' ';

            if (left) {
                for (int i = 0; i < plen; i++) printk_putc(prefix[i]);
                printk_puts(padded);
                for (int i = total; i < width; i++) printk_putc(' ');
            } else {
                for (int i = 0; i < plen; i++) printk_putc(prefix[i]);
                for (int i = total; i < width; i++) printk_putc(pad);
                printk_puts(padded);
            }
            fmt++;
            continue;
        }
        case 'p': {
            uintptr_t p = (uintptr_t)va_arg(ap, void *);
            if (p == 0) {
                printk_pad("(nil)", width, ' ', left);
            } else {
                char numbuf[72];
                printk_utoa(p, 16, 0, numbuf);
                char tmp[80];
                int n = 0;
                tmp[n++] = '0'; tmp[n++] = 'x';
                for (int i = 0; numbuf[i]; i++) tmp[n++] = numbuf[i];
                tmp[n] = 0;
                printk_pad(tmp, width, ' ', left);
            }
            fmt++;
            continue;
        }
        default:
            printk_putc('%');
            if (c) { printk_putc(c); fmt++; }
            break;
        }
    }
    va_end(ap);
}