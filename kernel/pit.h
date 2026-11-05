#ifndef PIT_H
#define PIT_H

void pit_init(unsigned int freq_hz);
extern volatile unsigned long pit_ticks;

#endif