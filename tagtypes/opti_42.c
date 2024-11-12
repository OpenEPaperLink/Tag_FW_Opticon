#include "hal.h"
#include "printf.h"
#include "dma.h"
#include "uc_var_bwry.h"
#include "delay.h"
#include <stdarg.h>
#include "drawing.h"
#include <stdlib.h>
#include <string.h>

#include "bitmaps.h"

#include "userinterface.h"
#include "powermgt.h"

#include "settings.h"
#include "screen.h"
#include "wdt.h"

#define DEBUGEPD

// Port P0
#define EPD_MISO 2
#define EPD_MOSI 3
#define EPD_CLK 5
#define EPD_RESET 0
#define EPD_DC 4
#define EPD_CS 7
#define EPD_CS2 6
#define EPD_BUSY 1

#define EPD_VPP 1  // P1.1

#define EPD_POWER_SENSE 0  // P1.0
#define EPD_POWER_1 2      // P1.2
#define EPD_POWER_2 3      // P1.3

// known EPD commands

void epdSend(uint8_t c) {
    U0DBUF = c;
    while (U0CSR & 0x01);
}

inline void epdSendNoWait(uint8_t c) {
    U0DBUF = c;
}

uint8_t epdRecv(uint8_t c) {
    U0DBUF = c;
    while (U0CSR & 0x01);
    return U0DBUF;
}

void epdSelect(void) {
    P0 &= ~(1 << 7);
}
void epdDeselect(void) {
    P0 |= (1 << 7);
}
void epdCommand(void) {
    P0 &= ~(1 << 4);
}
void epdData(void) {
    P0 |= (1 << 4);
}

void waitBusyHigh(uint16_t timeout) {
    delay_ms(1);
    while (P0 & (1 << 1)) {
        timeout--;
        delay_ms(1);
        if (!timeout) {
#ifdef DEBUGEPD
            pr("EPD: Timeout waiting for busy\n");
#endif
        }
    }
}

void waitBusyRefresh(uint16_t timeout) {
    delay_ms(1);
    IRCON &= ~0x20;  // Clear bit 5 to reset the Port 0 interrupt flag
    P0IFG &= ~0x02;  // Clear bit 1 to reset the P0.1 interrupt flag
    P0IEN |= 0x02;   // Enable P0.1 interrupt
    PICTL |= 0x01;   // Falling edge on port 0
    IEN1 |= 0x20;    // Enable Port 0 interrupt

    PM2Sleep(timeout);
    while (CLKCONSTA & CLKCONCMD_OSC);
    if (P0 & (1 << 1)) {
#ifdef DEBUGEPD
        pr("EPD: Timeout waiting for busy\n");
#endif
    }
    IEN1 &= ~(1 << P0IE);
    P0IEN &= ~(1 << 1);
}

void epdReset(void) {
    delay_ms(1);
    P0 &= ~(1 << 0);
    delay_ms(1);
    P0 |= (1 << 0);
    waitBusyHigh(10);
}

uint8_t epdRead(uint8_t reg) {
    epdCommand();
    epdSelect();
    epdSend(reg);
    epdData();
    uint8_t __xdata c = epdRecv(0x00);
    epdDeselect();
    return c;
}

void epdWrite(uint8_t reg, uint8_t len, ...) {
    va_list valist;
    va_start(valist, len);
    epdCommand();
    epdSelect();
    epdSend(reg);
    epdData();
    for (uint8_t i = 0; i < len; i++) {
        epdSend(va_arg(valist, int));
    }
    epdDeselect();
    va_end(valist);
}

void epdEnable(void) {
    U0CSR &= ~(UCSR_MODE);
    U0BAUD = 0;
    U0GCR |= 16;
    U0GCR |= (1 << 5);

    P0 |= (1 << EPD_RESET);
    P0 |= (1 << EPD_CS);

    P0 &= ~((1 << EPD_MISO) | (1 << EPD_BUSY));
    P0DIR &= ~((1 << EPD_MISO) | (1 << EPD_BUSY));
    P0DIR |= (1 << EPD_MOSI) | (1 << EPD_CLK) | (1 << EPD_RESET) | (1 << EPD_DC) | (1 << EPD_CS);
    P0SEL |= (1 << EPD_MOSI) | (1 << EPD_CLK);  // spi enabled on pins
    delay_ms(1);

    P1 |= (1 << EPD_POWER_1);
    P1 &= ~(1 << EPD_POWER_2);
    P1DIR |= (1 << EPD_POWER_1) | (1 << EPD_POWER_2);

    // primary 3v battery mosfet high
    P1 |= (1 << EPD_POWER_2);

    // shutdown 3v boost converter
    P1 &= ~(1 << EPD_POWER_1);

    /*
        uint8_t timeout = 100;
        while (!(P1 & (1 << EPD_POWER_SENSE))) {
            timeout--;
            delay_ms(1);
            if (!timeout) {
    #ifdef DEBUGEPD
                pr("EPD: Timeout waiting for power\n");
    #endif
            }
        }
    */

    // power battery mosfet
    P1 &= ~(1 << EPD_POWER_2);

    // start 3v boost converter
    P1 |= (1 << EPD_POWER_1);
    delay_ms(1);
}

void epdInit(void) {
#ifdef DEBUGEPD
    pr("EPD: Starting INIT\n");
#endif

    uint8_t w_high = (uint8_t)(SCREEN_WIDTH / 256);
    uint8_t w_low = (uint8_t)(SCREEN_WIDTH % 256);
    uint8_t h_high = (uint8_t)(SCREEN_HEIGHT / 256);
    uint8_t h_low = (uint8_t)(SCREEN_HEIGHT % 256);

    epdDeselect();
    epdReset();
    epdReset();
    delay_ms(1);
    // #ifdef DEBUGEPD
    // pr("LUT=%02X\n", epdRead(EPD_GET_LUT));
    epdWrite(0x12, 0);
    waitBusyHigh(1000);
    epdWrite(0x1B, 1, 0x7F);
    epdWrite(0x74, 1, 0x54);
    epdWrite(0x7E, 1, 0x3B);
    epdWrite(0x2B, 2, 0x04, 0x63);
    epdWrite(0x0C, 4, 0x8E, 0x8C, 0x85, 0x3F);
    epdWrite(0x01, 3, 0x2B, 0x01, 0x00);
    epdWrite(0x18, 1, 0x80);
    epdWrite(0x21, 2, 0x08, 0x80);
    epdWrite(0x22, 1, 0xB1);
    epdWrite(0x20, 0);
    waitBusyHigh(100);
    epdWrite(0x1B, 2, 0x15, 0x00);

#ifdef DEBUGEPD
    pr("EPD: Init complete\n");
#endif
}

void epdEnterSleep(void) {
#ifdef DEBUGEPD
    pr("EPD: Entering sleep\n");
#endif
    epdWrite(0x10, 1, 0x01);
    delay_us(30);

    // primary 3v battery mosfet high
    P1 |= (1 << EPD_POWER_2);
    // shutdown 3v boost converter
    P1 &= ~(1 << EPD_POWER_1);
    delay_us(3);
    P1DIR &= ~((1 << EPD_POWER_1) | (1 << EPD_POWER_2));

    P0SEL &= ~((1 << EPD_MOSI) | (1 << EPD_CLK));  // spi disabled on pins
    P0SEL = 0;
    P0DIR = 0;
    P2INP |= (1 << 5) | (1 << 7);
    P0 = 0;
    delay_us(1);
}

unsigned char reverse(unsigned char b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

void epdRenderDrawList(void) __reentrant {
    startWatch();

    __xdata uint8_t* buffer = malloc(SCREEN_WIDTH / 8);
    for (uint8_t color = 0; color < 2; color++) {
        epdWrite(0x11, 1, 0x03);
        epdWrite(0x44, 2, 0x00, 0x31);
        epdWrite(0x45, 4, 0x00, 0x00, 0x2B, 0x01);
        epdWrite(0x4E, 2, 0x00, 0x00);
        epdWrite(0x4F, 3, 0x00, 0x00, 0x00);
        epdWrite(0x24 + (2 * color), 0);  // 0x26;

        for (uint16_t y = SCREEN_HEIGHT - 1; y <= SCREEN_HEIGHT; y--) {
            wdtPet();
            dmaMemSet(buffer, 0x00, SCREEN_WIDTH / 8);
            getLine(y, color, buffer);
            epdSelect();
            for (uint16_t x = 0; x < SCREEN_WIDTH / 8; x++) {
                epdSend(reverse(buffer[x]));
            }
            epdDeselect();
        }
    }
    free(buffer);

#ifdef DEBUGEPD
    pr("EPD: Render complete in ");
    stopWatch();
#endif
}

void epdRefresh(void) {
    epdRenderDrawList();
    wdtPet();
    clearDrawList();
    epdWrite(0x22, 1, 0xC7);
    epdWrite(0x20, 0);
    waitBusyRefresh(50000);
    delay_ms(30);
    wdtPet();
}

void epdDisplay(void) {
    epdEnable();
    epdInit();
    epdRefresh();
#ifdef DEBUGEPD
    pr("EPD: refresh done\n");
#endif
    epdEnterSleep();
#ifdef DEBUGEPD
    pr("EPD: entered sleep\n");
#endif
}
