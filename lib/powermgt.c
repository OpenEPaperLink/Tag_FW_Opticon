#include "hal.h"

#include <stdint.h>

#include <stdbool.h>

#include "softuart.h"

#include "powermgt.h"
#include "meinradio.h"

#include "delay.h"
#include "eeprom.h"

#include "printf.h"

#include "dma.h"

#include "delay.h"

#define __packed
#include "../shared/oepl-proto.h"
#include "../shared/oepl-definitions.h"

#include "settings.h"

#include "proto.h"

#define LED 0

#define TIMER_TICKS_PER_MS 32

uint8_t __xdata capabilities;

uint16_t __xdata dataReqAttemptArr[POWER_SAVING_SMOOTHING] = {0};  // Holds the amount of attempts required per data_req/check-in
uint8_t __xdata dataReqAttemptArrayIndex = 0;
uint8_t __xdata dataReqLastAttempt = 0;
uint16_t __xdata nextCheckInFromAP = 0;
uint8_t __xdata wakeUpReason = 0;
uint8_t __xdata scanAttempts = 0;

int8_t __xdata temperature = 0;
uint16_t __xdata batteryVoltage = 2600;
bool __xdata lowBattery = false;
uint16_t __xdata longDataReqCounter = 0;
uint16_t __xdata voltageCheckCounter = 0;

#ifdef HAS_LED
    #define LED 0
bool __xdata ledBlink = false;
#endif

uint8_t __xdata currentPwrStatus = 0;

void clock_init(void) {
    /* Make sure we know where we stand */
    CLKCONCMD = CLKCONCMD_OSC32K | CLKCONCMD_OSC;
    while (!(CLKCONSTA & CLKCONCMD_OSC));

    /* Stay with 32 KHz RC OSC, change system clock to 32 MHz */
    CLKCONCMD &= ~CLKCONCMD_OSC;
    while (CLKCONSTA & CLKCONCMD_OSC);

    // CLKCONCMD |= (7 << 3);  // 250 kHz timer ticks
}

void powerUp(const uint8_t parts) {
    if (parts & INIT_BASE) {
        clock_init();
        FCTL = FCTL_CM0;
        setupDMA();
        wakeUpReason = getFirstWakeUpReason();
    }
    if (parts & INIT_UART) {
        initSoftUart();
    }
    if (parts & INIT_EEPROM) {
        eepromSetupPins(true);
        eepromInit();
    }
    if (parts & INIT_TEMPREADING) {
    }
    if (parts & INIT_RADIO) {
        radioInit();
        radioOn();
    }
    currentPwrStatus |= parts;
}

void powerDown(uint8_t parts) {
    parts &= currentPwrStatus;
    if (parts & INIT_UART) {
        shutdownUart();
    }
    if (parts & INIT_RADIO) {
        radioOff();
    }
    if (parts & INIT_EEPROM) {
        eepromDeepPowerDown();
        eepromSetupPins(false);
    }
    currentPwrStatus &= ~parts;
}

void PM2Sleep(const uint32_t __xdata ms) {
    //    P1 &= ~(1 << 0);
    uint32_t timer_value = ST0;
    timer_value += ((unsigned long int)ST1) << 8;
    timer_value += ((unsigned long int)ST2) << 16;
    timer_value += (ms * 32);
    ST2 = (unsigned char)(timer_value >> 16);
    ST1 = (unsigned char)(timer_value >> 8);
    ST0 = (unsigned char)timer_value;
    EA = 1;
    STIE = 1;

    // enter PM2
    SLEEPCMD |= 0x06;
    __asm
        .bndry 4;
    mov 0x87, #0x01;
    nop;
    __endasm;

    // while (CLKCONSTA & CLKCONCMD_OSC);
}

void PM1Sleep(uint8_t ms) {
    uint32_t timer_value = ST0;
    timer_value += ((unsigned long int)ST1) << 8;
    timer_value += ((unsigned long int)ST2) << 16;
    timer_value += (ms * 32);
    ST2 = (unsigned char)(timer_value >> 16);
    ST1 = (unsigned char)(timer_value >> 8);
    ST0 = (unsigned char)timer_value;
    EA = 1;
    STIE = 1;

    // enter PM1
    SLEEPCMD |= 0x05;
    __asm
        .bndry 4;
    mov 0x87, #0x01;
    nop;
    __endasm;
}

#define SLEEP_MAX_CHUNK 0x7FFFF
#define SLEEP_BLINK_PERIOD 750
void doActualSleep(uint32_t __xdata ms) {
    while (ms) {
        uint32_t maxTime;

#ifdef HAS_LED
        if ((ms > SLEEP_BLINK_PERIOD) && ledBlink && currentChannel) {
            P1 |= (1 << LED);
            PM2Sleep(1);
            P1 &= ~(1 << 0);
            maxTime = SLEEP_BLINK_PERIOD;
        } else {
            maxTime = SLEEP_MAX_CHUNK;
        }
#else
        maxTime = SLEEP_MAX_CHUNK;
#endif

        if (ms > maxTime) {
            PM2Sleep(maxTime);
            ms -= maxTime;
        } else {  //} if (ms > 3) {
            PM2Sleep(ms);
            ms = 0;
        }  // else {
           // PM1Sleep(ms);
           // ms = 0;
        //}
    }
    while (CLKCONSTA & CLKCONCMD_OSC);
    powerUp(INIT_UART);
    //    pr("wake");
}

void doSleep(const uint32_t __xdata ms) {
    //    pr("slp%lu\n", ms);
    doActualSleep(ms);
}

void sleepTimerVect(void) __interrupt(5) {
    IRCON &= ~(1 << 7);
}

void gpioWakeInt(void) __interrupt(13) {
    IRCON &= ~(1 << 5);
}

void doVoltageReading(void) {
    // powerUp(INIT_RADIO);  // load down the battery using the radio to get a good voltage reading
    delay_us(20);
    uint8_t step = 0;
    for (uint8_t c = 4; c < 31; c++) {
        BATTMON = 0 | (c << 1);
        delay_us(100);
        // pr("Step %d, Bat: %02X\n", c, BATTMON & 0x40);
        if (!(BATTMON & 0x40)) {
            pr("Step %d, Bat: %02X\n", c, BATTMON & 0x40);
            step = c;
            break;
        }
        BATTMON |= 1;
        delay_us(100);
    }
    BATTMON |= 1;
    switch (step) {
        case 19:
            batteryVoltage = 2300;
            break;
        case 20:
            batteryVoltage = 2350;
            break;
        case 21:
            batteryVoltage = 2400;
            break;
        case 22:
            batteryVoltage = 2450;
            break;
        case 23:
            batteryVoltage = 2500;
            break;
        case 24:
            batteryVoltage = 2550;
            break;
        case 25:
            batteryVoltage = 2600;
            break;
        case 26:
            batteryVoltage = 2700;
            break;
        case 27:
            batteryVoltage = 2800;
            break;
        case 28:
            batteryVoltage = 2950;
            break;
        case 29:
            batteryVoltage = 3100;
            break;
        case 30:
            batteryVoltage = 3200;
            break;
        default:
            batteryVoltage = 3001;
            break;
    }
    if(batteryVoltage<2800){
        lowBattery = true;
    } else {
        lowBattery = false;
    }
}

uint32_t getNextScanSleep(const bool increment) {
    if (increment) {
        if (scanAttempts < 255)
            scanAttempts++;
    }

    if (scanAttempts < INTERVAL_1_ATTEMPTS) {
        return INTERVAL_1_TIME;
    } else if (scanAttempts < (INTERVAL_1_ATTEMPTS + INTERVAL_2_ATTEMPTS)) {
        return INTERVAL_2_TIME;
    } else {
        return INTERVAL_3_TIME;
    }
}

void addAverageValue(void) {
    uint16_t __xdata curval = INTERVAL_AT_MAX_ATTEMPTS - INTERVAL_BASE;
    curval *= dataReqLastAttempt;
    curval /= DATA_REQ_MAX_ATTEMPTS;
    curval += INTERVAL_BASE;
    dataReqAttemptArr[dataReqAttemptArrayIndex % POWER_SAVING_SMOOTHING] = curval;
    dataReqAttemptArrayIndex++;
}

uint16_t getNextSleep(void) {
    uint16_t avg = 0;
    for (uint8_t c = 0; c < POWER_SAVING_SMOOTHING; c++) {
        avg += dataReqAttemptArr[c];
    }
    avg /= POWER_SAVING_SMOOTHING;

    // check if we should sleep longer due to an override in the config
    if (avg < tagSettings.minimumCheckInTime) return tagSettings.minimumCheckInTime;
    return avg;
}

void initPowerSaving(const uint16_t initialValue) {
    for (uint8_t c = 0; c < POWER_SAVING_SMOOTHING; c++) {
        dataReqAttemptArr[c] = initialValue;
    }
}

uint8_t getFirstWakeUpReason(void) {
    uint8_t reason = SLEEPSTA;
    reason >= 3;
    if (reason == 0) {
        return WAKEUP_REASON_FIRSTBOOT;
    } else if (reason == 2) {
        return WAKEUP_REASON_WDT_RESET;
    }  // we'll need to think of a better reason than this
    return WAKEUP_REASON_WDT_RESET;
}