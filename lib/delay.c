#include <stdint.h>
#include "hal.h"
#include "printf.h"
#include "delay.h"

uint32_t __xdata lastTime;
void startWatch(void) {
    lastTime = sleepTimerGet();
}


void stopWatch(void) {
    uint32_t __xdata cur = sleepTimerGet();
    pr("%lu ticks\n", (uint32_t)((cur - lastTime) / 32));
    lastTime = cur;
}

uint16_t timerGet(void) {
    uint16_t cur = ST0;
    cur |= ST1 << 8;
    return cur;
}

uint32_t sleepTimerGet(void){
    uint32_t timer_value = ST0;
    timer_value += ((unsigned long int)ST1) << 8;
    timer_value += ((unsigned long int)ST2) << 16;
    return timer_value;
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
