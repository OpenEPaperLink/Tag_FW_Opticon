#ifndef DELAY_H
#define DELAY_H

#include <stdint.h>

extern void startWatch(void);
extern void timerStart(void);
extern void stopWatch(void);
extern void delay_us(uint16_t len);
extern void delay_ms(uint16_t len);
extern uint16_t timerGet(void);
extern void timerDelay(uint16_t);

#define TIMER_TICKS_PER_MS 32

#endif