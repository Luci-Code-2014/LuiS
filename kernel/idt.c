#define idt_entries 256
extern void print(const char* str, unsigned char color);

typedef struct {
    unsigned short offset_low;
    unsigned short selector;
    unsigned char ist;
    unsigned char type_attr;
    unsigned short offset_mid;
    unsigned int offset_high;
    unsigned int reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    unsigned short limit;
    unsigned long base;
} __attribute__((packed)) idt_ptr_t;

idt_entry_t idt[idt_entries];
idt_ptr_t idt_ptr;

extern void load_idt(idt_ptr_t* ptr);
extern void keyboard_handler(void);
extern void enable_keyboard(void);

void set_idt_entry(int num, unsigned long handler, unsigned short selector, unsigned char type_attr) {
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;
    idt[num].type_attr = type_attr;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].reserved = 0;
}

void init_idt(void) {
    // set_idt_entry(33, (unsigned long)keyboard_handler, 0x18, 0x8E);

    // idt_ptr.limit = sizeof(idt) - 1;
    // idt_ptr.base = (unsigned long)idt;
    // load_idt(&idt_ptr);

    // enable_keyboard();
}

void keyboard_callback(unsigned char scancode) {
    char ascii = 0;
    
    if (scancode >= 0x10 && scancode <= 0x19) {
        char table[] = "QWERTYUIOP";
        ascii = table[scancode - 0x10];
    }
    else if (scancode >= 0x1E && scancode <= 0x26) {
        char table[] = "ASDFGHJKL";
        ascii = table[scancode - 0x1E];
    }
    else if (scancode >= 0x2C && scancode <= 0x32) {
        char table[] = "ZXCVBNM";
        ascii = table[scancode - 0x2C];
    }
    else if (scancode >= 0x02 && scancode <= 0x0B) {
        char table[] = "1234567890";
        ascii = table[scancode - 0x02];
    }
    else if (scancode == 0x1C) {
        ascii = '\n';
    }
    else if (scancode == 0x39) {
        ascii = ' ';
    }
    
    if (ascii != 0) {
        char str[2] = {ascii, 0};
        print(str, 0x0F);
    }
}