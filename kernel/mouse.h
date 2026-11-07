// kernel/mouse.h
#ifndef MOUSE_H
#define MOUSE_H

void mouse_init(void);
void mouse_handle_irq(void);
int mouse_get_x(void);
int mouse_get_y(void);
int mouse_get_clicked(void);

#endif