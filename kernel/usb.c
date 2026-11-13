// kernel/usb.c
#include "usb.h"
#include "printk.h"

static inline void outl(uint16_t port, uint32_t v) { __asm__ volatile ("outl %0, %1" :: "a"(v), "Nd"(port)); }
static inline uint32_t inl(uint16_t port) { uint32_t v; __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC));
    return inl(0xCFC);
}
static void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v) {
    outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC));
    outl(0xCFC, v);
}

/* ==================== 全局状态 ==================== */
int usb_max_ports = 0;
int usb_connected_count = 0;
uint32_t usb_port_status[32] = {0};

/* ==================== 对齐内存池 ==================== */
static uint8_t usb_mem_pool[128 * 1024] __attribute__((aligned(4096)));
static uint32_t usb_mem_ptr = 0;

static void *usb_alloc(uint32_t size) {
    if (usb_mem_ptr + size > sizeof(usb_mem_pool)) return 0;
    void *ret = &usb_mem_pool[usb_mem_ptr];
    usb_mem_ptr += size;
    usb_mem_ptr = (usb_mem_ptr + 63) & ~63; // 64字节对齐
    return ret;
}

#define EHCI_CAPLENGTH    0x00
#define EHCI_HCSPARAMS    0x04
#define EHCI_HCCPARAMS    0x08
#define EHCI_USBCMD       0x00 // OP 偏移
#define EHCI_USBSTS       0x04
#define EHCI_USBINTR      0x08
#define EHCI_ASYNCLISTADDR 0x18
#define EHCI_CONFIGFLAG   0x40
#define EHCI_PORTSC       0x44

#define EHCI_USBCMD_RUN   (1 << 0)
#define EHCI_USBCMD_HCRST (1 << 1)
#define EHCI_USBSTS_HCH   (1 << 0)

#define EHCI_PORTSC_CCS   (1 << 0)
#define EHCI_PORTSC_CSC   (1 << 1)
#define EHCI_PORTSC_PED   (1 << 2)
#define EHCI_PORTSC_PR    (1 << 8)

// EHCI 内存结构 (必须 32 字节对齐)
typedef struct {
    uint32_t horiz;
    uint32_t ep_cap[2];
    uint32_t ep_curr;
    uint32_t next_qtd;
    uint32_t alt_next;
    uint32_t token;
    uint32_t buf[5];
} __attribute__((aligned(32))) ehci_qh_t;

static volatile uint32_t *ehci_op = 0;
static ehci_qh_t *ehci_async_qh = 0;

static void ehci_init(volatile uint8_t *bar) {
    uint8_t caplength = bar[EHCI_CAPLENGTH];
    ehci_op = (volatile uint32_t *)(bar + caplength);

    uint32_t hcsparams = *(volatile uint32_t *)(bar + EHCI_HCSPARAMS);
    usb_max_ports = hcsparams & 0xF;
    printk("%[I] usb: [EHCI] 端口数=%d\n", usb_max_ports);

    // 1. 直接发送复位信号 (跳过无意义的停止等待)
    printk("%[I] usb: 复位 EHCI 控制器...\n");
    ehci_op[EHCI_USBCMD / 4] |= EHCI_USBCMD_HCRST;
    
    // 等待复位完成 (加入超时保护)
    for (int i = 0; i < 1000000; i++) {
        if ((ehci_op[EHCI_USBCMD / 4] & EHCI_USBCMD_HCRST) == 0) break;
        if (i == 999999) { 
            printk("%[E] usb: EHCI 复位超时，跳过初始化\n"); 
            return; 
        }
    }
    for (volatile int i = 0; i < 100000; i++); // 复位后休息一下

    // 2. 建立异步链表
    ehci_async_qh = (ehci_qh_t *)usb_alloc(sizeof(ehci_qh_t));
    for (uint32_t i = 0; i < sizeof(ehci_qh_t); i++) ((uint8_t*)ehci_async_qh)[i] = 0;
    
    ehci_async_qh->horiz = (uint32_t)(uintptr_t)ehci_async_qh | (1 << 1);
    ehci_async_qh->ep_cap[0] = (1 << 28) | (64 << 16); // 高速, 最大包长64
    
    ehci_op[EHCI_ASYNCLISTADDR / 4] = (uint32_t)(uintptr_t)ehci_async_qh;
    ehci_op[EHCI_USBINTR / 4] = 0;

    // 3. 启动控制器
    ehci_op[EHCI_CONFIGFLAG / 4] = 1;
    ehci_op[EHCI_USBCMD / 4] = (8 << 16) | EHCI_USBCMD_RUN;
    printk("%[O] usb: EHCI 启动完成！\n");

    // 4. 扫描端口并唤醒
    usb_connected_count = 0;
    volatile uint32_t *portsc = (volatile uint32_t *)((uint8_t *)ehci_op + EHCI_PORTSC);
    
    for (int i = 0; i < usb_max_ports; i++) {
        uint32_t status = portsc[i];
        
        if (status & EHCI_PORTSC_CCS) {
            // 清除变化位并发送复位
            portsc[i] = status | EHCI_PORTSC_CSC; 
            portsc[i] |= EHCI_PORTSC_PR;          
            
            for (int t = 0; t < 1000000; t++) {
                if ((portsc[i] & EHCI_PORTSC_PR) == 0) break;
                if (t == 999999) {
                    printk("%[E] usb: 端口 %d 复位超时\n", i + 1);
                }
            }
            for (volatile int d = 0; d < 100000; d++);
        }
        
        usb_port_status[i] = portsc[i];
        if (portsc[i] & EHCI_PORTSC_CCS) {
            printk("%[O] usb: [EHCI] 端口 %d 检测到设备连接, PORTSC=0x%x\n", i + 1, portsc[i]);
            usb_connected_count++;
        } else {
            printk("%[I] usb: [EHCI] 端口 %d 空闲 PORTSC=0x%x\n", i + 1, portsc[i]);
        }
    }
}

#define XHCI_CAPLENGTH     0x00
#define XHCI_HCSPARAMS1    0x04
#define XHCI_HCCPARAMS1    0x10
#define XHCI_DBOFF         0x14
#define XHCI_RTSOFF        0x18
#define XHCI_USBCMD        0x00
#define XHCI_USBSTS        0x04
#define XHCI_CRCR          0x18
#define XHCI_DCBAAP        0x30
#define XHCI_CONFIG        0x38

#define USBCMD_RUN         (1 << 0)
#define USBCMD_HCRST       (1 << 1)
#define USBCMD_INTE        (1 << 2)
#define USBSTS_CNR         (1 << 11)
#define PORTSC_CCS         (1 << 0)
#define PORTSC_PED         (1 << 1)

typedef struct {
    uint32_t param_lo;
    uint32_t param_hi;
    uint32_t status;
    uint32_t control;
} __attribute__((packed)) trb_t;

static volatile uint32_t *xhci_op  = 0;
static uint32_t max_slots = 0;
static trb_t *cmd_ring = 0;
static uint64_t *dcbaa = 0;

static void xhci_init(volatile uint8_t *bar) {
    xhci_op = (volatile uint32_t *)(bar + bar[0]);

    uint32_t hcsparams1 = *(volatile uint32_t *)(bar + XHCI_HCSPARAMS1);
    usb_max_ports = hcsparams1 & 0xFF;
    max_slots = (hcsparams1 >> 24) & 0xFF;
    if (usb_max_ports > 32) { uint32_t tmp = usb_max_ports; usb_max_ports = max_slots; max_slots = tmp; }

    printk("%[I] usb: [xHCI] 端口=%d, 槽位=%d\n", usb_max_ports, max_slots);

    xhci_op[XHCI_USBCMD / 4] = USBCMD_HCRST;
    while (xhci_op[XHCI_USBCMD / 4] & USBCMD_HCRST);
    while (xhci_op[XHCI_USBSTS / 4] & USBSTS_CNR);

    dcbaa = (uint64_t *)usb_alloc(256 * 8);
    cmd_ring = (trb_t *)usb_alloc(256 * 16);
    for (int i = 0; i < 2048; i++) ((uint8_t *)dcbaa)[i] = 0;
    for (int i = 0; i < 4096; i++) ((uint8_t *)cmd_ring)[i] = 0;

    xhci_op[XHCI_DCBAAP / 4] = (uint32_t)(uintptr_t)dcbaa;
    xhci_op[XHCI_CRCR / 4] = (uint32_t)(uintptr_t)cmd_ring | 1;
    xhci_op[XHCI_CONFIG / 4] = max_slots;
    xhci_op[XHCI_USBCMD / 4] = USBCMD_RUN | USBCMD_INTE;

    printk("%[O] usb: xHCI 启动完成！\n");

    volatile uint32_t *portsc = (volatile uint32_t *)((uint8_t *)xhci_op + 0x400);
    usb_connected_count = 0;

    for (int i = 0; i < usb_max_ports; i++) {
        uint32_t status = portsc[i];
        if (status & (1 << 9)) {
            portsc[i] = (status & ~(0xE01C)) | PORTSC_PED;
            for (volatile int d = 0; d < 100000; d++);
            status = portsc[i];
            portsc[i] = (status & ~(0xE01C)) | (1 << 4);
            for (int t = 0; t < 1000000; t++) {
                if (!(portsc[i] & (1 << 4))) break;
            }
            for (volatile int d = 0; d < 100000; d++);
            status = portsc[i];
        }
        usb_port_status[i] = status;
        if (status & PORTSC_CCS) usb_connected_count++;
    }
}

/* ==================== 统一初始化入口 ==================== */
void usb_init(void) {
    printk("%[I] usb: 开始扫描 PCI 总线...\n");
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read32(bus, dev, fn, 0x00);
                if (id == 0xFFFFFFFF) continue;
                
                uint32_t cls = pci_read32(bus, dev, fn, 0x08);
                if (((cls >> 24) & 0xFF) == 0x0C && ((cls >> 16) & 0xFF) == 0x03) {
                    uint32_t prog_if = (cls >> 8) & 0xFF;
                    uint32_t bar0 = pci_read32(bus, dev, fn, 0x10) & 0xFFFFFFF0;
                    
                    uint32_t cmd = pci_read32(bus, dev, fn, 0x04);
                    pci_write32(bus, dev, fn, 0x04, cmd | 0x0006);

                    if (prog_if == 0x20) {
                        printk("%[I] usb: 发现 EHCI 控制器位于 %d:%d:%d\n", bus, dev, fn);
                        ehci_init((volatile uint8_t *)bar0);
                        return; // 找到并初始化成功就退出
                    } else if (prog_if == 0x30) {
                        printk("%[I] usb: 发现 xHCI 控制器位于 %d:%d:%d\n", bus, dev, fn);
                        xhci_init((volatile uint8_t *)bar0);
                        return;
                    }
                }
            }
        }
    }
}