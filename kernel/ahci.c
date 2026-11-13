// kernel/ahci.c
#include <stdint.h>
#include "ahci.h"
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

// 严格对齐的静态内存池
static uint8_t ahci_cmd_list[1024] __attribute__((aligned(1024)));
static uint8_t ahci_fis[256] __attribute__((aligned(256)));
static uint8_t ahci_cmd_table[256] __attribute__((aligned(128)));
static uint8_t ahci_dma_buf[512] __attribute__((aligned(512)));

typedef struct {
    uint32_t clb; uint32_t clbu;
    uint32_t fb;  uint32_t fbu;
    uint32_t is;  uint32_t ie;
    uint32_t cmd; uint32_t rsv0;
    uint32_t tfd; uint32_t sig;
    uint32_t ssts; uint32_t sctl;
    uint32_t serr; uint32_t sact;
    uint32_t ci;   uint32_t sntf;
    uint32_t fbs;  uint32_t rsv1[11];
    uint32_t vendor[4];
} __attribute__((packed)) hba_port_t;

typedef struct {
    uint32_t cap;  uint32_t ghc;
    uint32_t is;   uint32_t pi;
    uint32_t vs;   uint32_t ccc_ctl;
    uint32_t ccc_pts; uint32_t em_loc;
    uint32_t em_ctl;  uint32_t cap2;
    uint32_t bohc;
    uint8_t  rsv[0xA0 - 0x2C];
    uint8_t  vendor[0x100 - 0xA0];
    hba_port_t ports[32];
} __attribute__((packed)) hba_mem_t;

static volatile hba_mem_t *hba = 0;
static volatile hba_port_t *cmd_port = 0;

static int ahci_rw(uint32_t lba, void *buf, int is_write);

int ahci_init(void) {
    printk("%[I] ahci: 开始扫描 PCI 总线...\n");
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read32(bus, dev, fn, 0x00);
                if (id == 0xFFFFFFFF) continue;
                uint32_t cls = pci_read32(bus, dev, fn, 0x08);

                if (((cls >> 24) & 0xFF) == 0x01 && ((cls >> 16) & 0xFF) == 0x06) {
                    uint32_t pci_cmd = pci_read32(bus, dev, fn, 0x04);
                    pci_write32(bus, dev, fn, 0x04, pci_cmd | 0x0006);

                    uint32_t abar = pci_read32(bus, dev, fn, 0x24) & 0xFFFFFFF0;
                    if (!abar) continue;
                    hba = (hba_mem_t *)(uintptr_t)abar;

                    printk("%[I] ahci: 找到控制器 ABAR=0x%x, PI=0x%x\n", abar, hba->pi);
                    hba->ghc |= (1 << 31);

                    for (int i = 0; i < 32; i++) {
                        if (hba->pi & (1 << i)) {
                            volatile hba_port_t *p = &hba->ports[i];

                            p->sctl = 0x301;
                            for (volatile int d=0; d<100000; d++);
                            p->sctl = 0;
                            for (volatile int d=0; d<1000000; d++) {
                                if ((p->ssts & 0x0F) == 3) break;
                            }

                            printk("%[I] ahci: 探测端口 %d, SSTS=0x%x, SIG=0x%x\n", i, p->ssts, p->sig);

                            if ((p->ssts & 0x0F) == 3) {
                                p->cmd &= ~1;
                                p->cmd &= ~(1 << 4);
                                for (int t = 0; t < 100000; t++) {
                                    if (!(p->cmd & (1 << 15)) && !(p->cmd & (1 << 14))) break;
                                }

                                p->clb  = (uint32_t)(uintptr_t)ahci_cmd_list;
                                p->fb   = (uint32_t)(uintptr_t)ahci_fis;
                                p->serr = 0xFFFFFFFF;
                                p->is   = 0xFFFFFFFF;
                                p->cmd |= 1;
                                p->cmd |= (1 << 4);

                                cmd_port = p;

                                if (ahci_rw(2, ahci_dma_buf, 0) == 0) {
                                    __asm__ volatile ("mfence" ::: "memory");
                                    uint16_t magic = *(uint16_t *)(ahci_dma_buf + 0x38);
                                    if (magic == 0xEF53) {
                                        printk("%[O] ahci: 端口 %d 成功挂载 ext4!\n", i);
                                        return 0;
                                    } else {
                                        printk("%[W] ahci: 端口 %d 魔数不对(0x%x)，继续找...\n", i, magic);
                                    }
                                }
                                cmd_port = 0;
                            }
                        }
                    }
                    printk("%[W] ahci: 所有端口扫描完毕，未找到 ext4\n");
                    return -1;
                }
            }
        }
    }
    printk("%[E] ahci: 未找到 AHCI 控制器\n");
    return -1;
}

static int ahci_rw(uint32_t lba, void *buf, int is_write) {
    if (!cmd_port) return -1;

    for (int i = 0; i < 1000000; i++) {
        if (!(cmd_port->tfd & 0x88)) break;
        if (i == 999999) return -1;
    }

    cmd_port->is = 0xFFFFFFFF; 
    for (int i = 0; i < 1024; i++) ahci_cmd_list[i] = 0;
    for (int i = 0; i < 256; i++) ahci_cmd_table[i] = 0;

    uint32_t *hdr = (uint32_t *)ahci_cmd_list;
    hdr[0] = 5 | (1 << 16) | (is_write ? (1 << 6) : 0);
    hdr[2] = (uint32_t)(uintptr_t)ahci_cmd_table;

    uint8_t *fis = ahci_cmd_table;
    fis[0] = 0x27; 
    fis[1] = 0x80; 
    fis[2] = is_write ? 0xC4 : 0x25; 
    fis[3] = 0;
    fis[4] = (uint8_t)lba;
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 0x40; 
    fis[8] = 0; fis[9] = 0;
    fis[10] = 0; fis[11] = 0;
    fis[12] = 1; 
    fis[13] = 0; fis[14] = 0; fis[15] = 0;

    uint32_t *prdt = (uint32_t *)(ahci_cmd_table + 128);
    prdt[0] = (uint32_t)(uintptr_t)buf;
    prdt[3] = 511 | (1u << 31); 

    __asm__ volatile("mfence" ::: "memory"); 

    // 触发 AHCI 硬件
    cmd_port->ci = 1;
    
    // 给虚拟 DMA 引擎一点点时间启动
    for (volatile int d = 0; d < 1000; d++) {
        __asm__ volatile("pause");
    }

    for (int i = 0; i < 1000000; i++) {
        uint32_t tfd = cmd_port->tfd;
        uint32_t ci = cmd_port->ci;

        if (tfd & 0x01) {
            printk("%[E] ahci: 传输错误 TFD=0x%x\n", tfd);
            return -1;
        }
    
        if (!(ci & 1)) {
            __asm__ volatile("mfence" ::: "memory"); 
            return 0;
        }

        if ((tfd & 0x80) == 0 && (tfd & 0x08) == 0) {
            cmd_port->ci = 0;
            __asm__ volatile("mfence" ::: "memory"); 
            return 0;
        }
    }
    return -1;
}

int ahci_read_sector(uint32_t lba, void *buf) { return ahci_rw(lba, buf, 0); }
int ahci_write_sector(uint32_t lba, const void *buf) { return ahci_rw(lba, (void *)buf, 1); }