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

#include "wdt.h"

#include "settings.h"
#include "screen.h"

// Port P0
#define EPD_MISO 2
#define EPD_MOSI 3
#define EPD_CLK 5
#define EPD_RESET 0
#define EPD_DC 4
#define EPD_CS 7
#define EPD_CS2 6
#define EPD_BUSY 1

// Port P1
#define EPD_VPP 1  // P1.1

#define EPD_POWER_SENSE 0  // P1.0
#define EPD_POWER_1 2      // P1.2
#define EPD_POWER_2 3      // P1.3

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
    waitBusyHigh(1000);
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
    P0DIR |= (1 << EPD_MOSI) | (1 << EPD_CLK) | (1 << EPD_RESET) | (1 << EPD_DC) | (1 << EPD_CS) | (1 << EPD_CS2);
    P0SEL |= (1 << EPD_MOSI) | (1 << EPD_CLK);  // spi enabled on pins
    delay_ms(1);

    // VPP as input
    P1DIR &= ~(1 << EPD_VPP);

    P1 |= (1 << EPD_POWER_1);
    P1 &= ~(1 << EPD_POWER_2);
    P1DIR |= (1 << EPD_POWER_1) | (1 << EPD_POWER_2);

    // primary 3v battery mosfet high
    P1 |= (1 << EPD_POWER_2);

    // shutdown 3v boost converter
    P1 &= ~(1 << EPD_POWER_1);

    // power battery mosfet
    P1 &= ~(1 << EPD_POWER_2);

    // start 3v boost converter
    P1 |= (1 << EPD_POWER_1);
    delay_ms(1);
}

void epdInit(void) {
#ifdef DEBUGEPD
    pr("EPD: Starting Init\n");
#endif

    uint8_t w_high = (uint8_t)(SCREEN_WIDTH / 256);
    uint8_t w_low = (uint8_t)(SCREEN_WIDTH % 256);
    uint8_t h_high = (uint8_t)(SCREEN_HEIGHT / 256);
    uint8_t h_low = (uint8_t)(SCREEN_HEIGHT % 256);

    epdDeselect();
    epdReset();
    epdReset();
    epdReset();

    delay_ms(1);
    P0 |= (1 << EPD_CS2);

#ifdef DEBUGEPD
    pr("LUT=%02X\n", epdRead(EPD_GET_LUT));
    pr("Status=%02X\n", epdRead(EPD_GET_STATUS));
#else
    epdRead(EPD_GET_LUT);
    epdRead(EPD_GET_STATUS);
#endif
    epdWrite(EPD_POWER_SETTING, 4, 0x37, 0x00, 0x0b, 0x0b);
    epdWrite(EPD_BOOSTER_SOFTSTART, 3, 0x2e, 0x2e, 0x2e);
    epdWrite(EPD_POWER_ON, 0);
    waitBusyHigh(1500);
    epdWrite(EPD_TEMP_SENSE_ENABLE, 1, 0x00);
    epdWrite(EPD_TEMP_CALIB, 2);
    waitBusyHigh(1000);

    // read temperature data
    epdSelect();
    uint8_t __xdata c = epdRecv(0x00);
    c = epdRecv(0x00);
    epdDeselect();

    epdWrite(EPD_PANEL_SETTING, 1, 0xCF);
    epdWrite(0x2A, 2, 0x00, 0x00);
    epdWrite(0xE5, 1, 0x03);
    epdWrite(EPD_VCOM_INTERVAL, 1, 0x07);
    epdWrite(EPD_TCON_SETTING, 1, 0x22);
    epdWrite(EPD_RESOLUTION, 4, w_high, w_low, h_high, h_low);
    epdWrite(EPD_PLL_CONTROL, 1, 0x3E);
    epdWrite(0x82, 1, 0x24);

#ifdef DEBUGEPD
    pr("EPD: Init complete\n");
#endif
}

void epdEnterSleep(void) {
#ifdef DEBUGEPD
    pr("EPD: Entering sleep\n");
#endif
    epdWrite(0x50, 1, 0xF7);
    epdWrite(0x02, 0);
    delay_us(30);
    waitBusyHigh(1000);
    epdWrite(0x07, 1, 0xA5);
    P0 &= ~(1 << EPD_CS2);
    // primary 3v battery mosfet high
    P1 |= (1 << EPD_POWER_2);
    // shutdown 3v boost converter
    P1 &= ~(1 << EPD_POWER_1);
    delay_us(3);
    P1DIR &= ~((1 << EPD_POWER_1) | (1 << EPD_POWER_2));

    P0SEL &= ~((1 << EPD_MOSI) | (1 << EPD_CLK));  // spi disabled on pins
    P0SEL = 0;
    P0DIR = 0;
    P0INP |= (1 << EPD_BUSY) | (1 << EPD_MISO);
    P2INP |= (1 << 5);  //| (1 << 7);
    P0 = 0;
    delay_us(1);

    // drive VPP low, seems to reduce power
    P1 &= ~(1 << EPD_VPP);
    P1DIR |= (1 << EPD_VPP);
}

unsigned char reverse(unsigned char b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

inline void sendColor(uint8_t b, uint8_t r) {
    uint8_t b_out = 0;
    for (int8_t shift = 3; shift >= 0; shift--) {
        b_out = 0;
        if ((r >> 2 * shift) & 0x01) {
            b_out |= 0x07;
        } else if ((b >> 2 * shift) & 0x01) {
            b_out |= 0x03;
        }

        if ((r >> 2 * shift) & 0x02) {
            b_out |= 0x70;
        } else if ((b >> 2 * shift) & 0x02) {
            b_out |= 0x30;
        }
        epdSend(b_out);
    }
}

void epdRenderDrawList(void) __reentrant {
#ifdef DEBUGEPD
    startWatch();
#endif
    epdWrite(0x10, 0);

    __xdata uint8_t* black = malloc(SCREEN_WIDTH / 8);
    __xdata uint8_t* red = malloc(SCREEN_WIDTH / 8);

    for (uint16_t y = SCREEN_HEIGHT - 1; y <= SCREEN_HEIGHT; y--) {
        wdtPet();
        dmaMemSet(black, 0x00, SCREEN_WIDTH / 8);
        dmaMemSet(red, 0x00, SCREEN_WIDTH / 8);

        getLine(y, 0, black);
        getLine(y, 1, red);

        epdSelect();
        for (uint16_t x = 0; x < SCREEN_WIDTH / 8; x++) {
            sendColor(reverse(black[x]), reverse(red[x]));
        }
        epdDeselect();
    }
    free(black);
    free(red);

    epdRead(0x11);
#ifdef DEBUGEPD
    pr("EPD: Render complete in ");
    stopWatch();
#endif
}

void epdRefresh(void) {
    epdRenderDrawList();
    clearDrawList();
    epdWrite(0x13, 0);
    waitBusyRefresh(50000);
    wdtPet();
    delay_ms(30);
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
