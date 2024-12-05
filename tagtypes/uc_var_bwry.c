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

// Port P0
#define EPD_MISO 2
#define EPD_MOSI 3
#define EPD_CLK 5
#define EPD_RESET 0
#define EPD_DC 4
#define EPD_CS 7
#define EPD_BUSY 1

#define EPD_POWER 1  // P1.1

// #define EPD_WIDTH 128
// #define EPD_HEIGHT 296
// #define EPD_BPP 2

// known EPD commands
#define EPD_GET_LUT 0x71
#define EPD_GET_STATUS 0x70
#define EPD_PANEL_SETTING 0x00
#define EPD_POWER_SETTING 0x01
#define EPD_POWER_OFF 0x02
#define EPD_ENTER_SLEEP 0x07
#define EPD_POWER_ON 0x04
#define EPD_BOOSTER_SOFTSTART 0x06
#define EPD_TEMP_CALIB 0x40
#define EPD_TEMP_SENSE_ENABLE 0x41
#define EPD_POWER_OFF_SEQUENCE 0x03
#define EPD_VCOM_INTERVAL 0x50
#define EPD_TCON_SETTING 0x60
#define EPD_RESOLUTION 0x61
#define EPD_POWER_SAVING 0xE3
#define EPD_PLL_CONTROL 0x30
#define EPD_START_TRANSMISSION_DTM 0x10
#define EPD_DATA_STOP 0x11
#define EPD_REFRESH 0x12

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
    while (!(P0 & (1 << 1))) {
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
    PICTL &= ~0x01;  // Rising edge on port 0
    IEN1 |= 0x20;    // Enable Port 0 interrupt

    PM2Sleep(timeout);
    while (CLKCONSTA & CLKCONCMD_OSC);
    if (!(P0 & (1 << 1))) {
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
    waitBusyHigh(0);
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
    P1 &= ~(1 << EPD_POWER);  // enable EPD power

    P0 &= ~((1 << EPD_MISO) | (1 << EPD_BUSY));
    P0DIR &= ~((1 << EPD_MISO) | (1 << EPD_BUSY));
    P0DIR |= (1 << EPD_MOSI) | (1 << EPD_CLK) | (1 << EPD_RESET) | (1 << EPD_DC) | (1 << EPD_CS);
    P0SEL |= (1 << EPD_MOSI) | (1 << EPD_CLK);  // spi enabled on pins
    P1DIR |= (1 << EPD_POWER);
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
#ifdef DEBUGEPD
    pr("LUT=%02X\n", epdRead(EPD_GET_LUT));
    pr("Status=%02X\n", epdRead(EPD_GET_STATUS));
#else
    epdRead(EPD_GET_LUT);
    epdRead(EPD_GET_STATUS);
#endif
    epdWrite(EPD_POWER_SETTING, 2, 0x07, 0x00);
    epdWrite(EPD_BOOSTER_SOFTSTART, 7, 0x0F, 0x0A, 0x2F, 0x25, 0x22, 0x2E, 0x21);
    epdWrite(EPD_POWER_ON, 0x00);
    waitBusyHigh(1000);
    epdWrite(EPD_TEMP_SENSE_ENABLE, 1, 0x00);
    epdWrite(EPD_TEMP_CALIB, 0);
    waitBusyHigh(1000);
    epdWrite(0x16, 1, 0x00);
    epdWrite(EPD_PANEL_SETTING, 2, 0x0F, 0x49);
    epdWrite(0x4D, 1, 0x78);
    epdWrite(EPD_POWER_OFF_SEQUENCE, 3, 0x10, 0x54, 0x44);
    epdWrite(EPD_VCOM_INTERVAL, 1, 0x37);
    epdWrite(EPD_TCON_SETTING, 2, 0x02, 0x02);
    epdWrite(EPD_RESOLUTION, 4, w_high, w_low, h_high, h_low);
    epdWrite(0xE7, 1, 0x1C);
    epdWrite(EPD_POWER_SAVING, 1, 0x22);
    epdWrite(0xB4, 1, 0xD0);
    epdWrite(0xB5, 1, 0x03);
    epdWrite(0xE9, 1, 0x01);
    epdWrite(EPD_PLL_CONTROL, 1, 0x08);
#ifdef DEBUGEPD
    pr("EPD: Init complete\n");
#endif
}

void epdEnterSleep(void) {
    epdWrite(EPD_VCOM_INTERVAL, 1, 0xC7);
    epdWrite(EPD_POWER_OFF, 0);
    epdWrite(EPD_ENTER_SLEEP, 1, 0xA5);
    delay_us(30);
    P1 |= (1 << EPD_POWER);                        // disable EPD power
    P0SEL &= ~((1 << EPD_MOSI) | (1 << EPD_CLK));  // spi disabled on pins
    P0SEL = 0;
    P0DIR = 0;
    P2INP |= (1 << 5);
    P0 = 0;
    P1 |= (1 << EPD_POWER);  // disable EPD power
    delay_us(1);
    P1DIR &= ~(1 << EPD_POWER);
}

void epdRenderDrawList(void) __reentrant {
    startWatch();
    epdWrite(EPD_START_TRANSMISSION_DTM, 0);

    __xdata uint8_t* black = malloc(SCREEN_WIDTH / 8);
    __xdata uint8_t* red = malloc(SCREEN_WIDTH / 8);
    __xdata uint8_t* yellow = malloc(SCREEN_WIDTH / 8);

    for (uint16_t y = SCREEN_HEIGHT - 1; y <= SCREEN_HEIGHT; y--) {
        dmaMemSet(black, 0x00, SCREEN_WIDTH / 8);
        dmaMemSet(red, 0x00, SCREEN_WIDTH / 8);
        dmaMemSet(yellow, 0x00, SCREEN_WIDTH / 8);

        // load color information into separate buffers
        getLine(y, 0, black);
        getLine(y, 1, red);
        getLine(y, 2, yellow);
        wdtPet();
        epdSelect();
        for (uint16_t x = 0; x < SCREEN_WIDTH;) {
            // merge color buffers into one
            uint8_t temp = 0;
            for (uint8_t shift = 0; shift < 4; shift++) {
                temp <<= 2;
                uint8_t curByte = x / 8;
                uint8_t curMask = (1 << (x % 8));
                if ((red[curByte] & curMask)) {
                    temp |= 0x03;
                } else if (yellow[curByte] & curMask) {
                    temp |= 0x02;
                } else if (black[curByte] & curMask) {
                } else {
                    temp |= 0x01;
                }
                x++;
            }
            // send out buffer to EPD
            U0DBUF = temp;
        }
        while (U0CSR & 0x01);
        epdDeselect();
    }

    free(black);
    free(red);
    free(yellow);

    epdWrite(EPD_DATA_STOP, 1, 0x80);

#ifdef DEBUGEPD
    pr("EPD: Render complete in ");
    stopWatch();
#endif
}

void epdRefresh(void) {
    epdRenderDrawList();
    clearDrawList();
    epdWrite(EPD_REFRESH, 1, 0x00);
    waitBusyRefresh(50000);
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
