#ifndef POWERMGT_H
#define POWERMGT_H

#include "tagprofile.h"

#define INIT_EPD_VOLTREADING 0x80
#define INIT_RADIO 0x40
#define INIT_I2C 0x20
#define INIT_UART 0x10
#define INIT_EPD 0x08
#define INIT_EEPROM 0x04
#define INIT_TEMPREADING 0x02
#define INIT_BASE 0x01

extern void powerUp(const uint8_t parts);
extern void powerDown(uint8_t parts);
extern void doSleep(const uint32_t __xdata ms);
extern void PM2Sleep(const uint32_t __xdata ms);

#ifdef HAS_LED
    #define LED 0
extern bool __xdata ledBlink;
#endif

extern void sleepTimerVect(void) __interrupt(5);

extern void gpioWakeInt(void) __interrupt(13);

extern uint8_t checkButtonOrJig(void);

extern void setupPortsInitial(void);

extern void initAfterWake(void);

extern void doVoltageReading(void);

extern void addAverageValue(void);
extern uint16_t getNextSleep(void);

extern uint32_t getNextScanSleep(const bool increment);
extern void initPowerSaving(const uint16_t initialValue);
extern uint8_t getFirstWakeUpReason(void);

extern uint8_t __xdata wakeUpReason;

extern uint8_t __xdata capabilities;

extern uint16_t __xdata nextCheckInFromAP;
extern uint8_t __xdata dataReqLastAttempt;
extern int8_t __xdata temperature;
extern uint16_t __xdata batteryVoltage;
extern bool __xdata lowBattery;
extern uint8_t __xdata scanAttempts;
extern uint16_t __xdata longDataReqCounter;
extern uint16_t __xdata voltageCheckCounter;

#endif