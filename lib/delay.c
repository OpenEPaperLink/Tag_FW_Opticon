#include <stdint.h>
#include "hal.h"
#include "printf.h"
#include "delay.h"

uint16_t __xdata lastTime;
void startWatch(void) {
    lastTime = timerGet();
}


void stopWatch(void) {
    uint16_t __xdata cur = timerGet();
    pr("%lu ticks\n", (uint32_t)((cur - lastTime) / 32));
    lastTime = cur;
}

uint16_t timerGet(void) {
    uint16_t cur = ST0;
    cur |= ST1 << 8;
    return cur;
}

void delay_us(uint16_t len) {
    while (len--) {
        __asm nop
            __endasm;
    }
}

void delay_ms(uint16_t len) {
    while (len--) {
        delay_us(998);
    }
}

void timerDelay(uint16_t t) {
    delay_ms(t);
}
