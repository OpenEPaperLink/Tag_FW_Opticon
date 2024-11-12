#include "hal.h"
#include "softuart.h"
#include "delay.h"

void startWDT(void) {
#ifndef DISABLEWDT
    WDCTL |= 0x08;
#endif
}

void wdtPet(void) {
#ifndef DISABLEWDT
    WDCTL = 0xA8;
    WDCTL = 0x58;
#endif
}

void wdtReset(void) {
    WDCTL |= 0x08;
    WDCTL = 0x0B;
    while(1);
}