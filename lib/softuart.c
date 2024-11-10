#include "hal.h"
#include "softuart.h"
#include "delay.h"

inline void initSoftUart(void) {
    P2DIR |= (1 << 0);  // enable soft uart out
    //T4CTL = 0x9E; 9600
    T4CCTL0 |= (1 << 2);
    //T4CC0 = 208;  // 9600


    T4CTL = 0x3E;
    T4CC0 = 139; // 115200 baud
}

inline void shutdownUart(void) {
    P2DIR &= (1 << 0);
    P2 &= ~(1);
}

inline void uartPeriod(void) {
    TIMIF &= ~(1 << 4);
    while ((TIMIF & (1 << 4)) != 0x10);
}

void softUartSendByte(char u) {
    EA = 0;
    uartPeriod();
    P2 &= ~(1);
    // P2|=1;
    for (uint8_t c = 0; c < 8; c++) {
        uartPeriod();
        if (u & 0x01) {
            P2 |= 1;
        } else {
            P2 &= ~(1);
        }
        u >>= 1;
    }
    uartPeriod();
    P2 |= 1;
    //uartPeriod();
    EA = 1;
}
