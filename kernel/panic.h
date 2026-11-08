// kernel/panic.h
#ifndef PANIC_H
#define PANIC_H

// 接收异常名称, 错误码, RIP 和 RSP
void panic(const char *msg, unsigned long err, unsigned long rip, unsigned long rsp);

#endif