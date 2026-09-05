#define vga_address 0xB8000
#define vga_width 80
#define vga_height 25

void init_idt(void);

void putchar(char c, int x, int y, unsigned char color) {
    unsigned short* vga = (unsigned short*)vga_address;
    vga[y * vga_width + x] = (color << 8) | c;
}

void print(const char* str, unsigned char color) {
    static int x = 0, y = 0;
    for (int i = 0; str[i] != '\0'; i++) {
        char c = str[i];

        if (c == '\n') {
            x = 0;
            y++;
            continue;
        }
        putchar(c, x, y, color);
        x++;

        if (x >= vga_width) {
            x = 0;
            y++;
        }
    }
}

void kernel_main() {
    for (int i = 0; i < vga_width * vga_height; i++) {
        ((unsigned short*)vga_address)[i] = 0x0000;
    }

    print("Hello from C Kernel!\n", 0x0F);
    print("Press any key...\n", 0x0A);

    init_idt();

    while (1) {}
}