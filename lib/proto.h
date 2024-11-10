#ifndef PROTOH_H
#define PROTOH_H

#include <stdint.h>

extern uint8_t detectAP(const uint8_t channel) __reentrant;
extern __xdata struct AvailDataInfo * getAvailDataInfo(void) __reentrant;
extern bool processAvailDataInfo(__xdata struct AvailDataInfo *avail) __reentrant;

extern uint8_t __xdata mSelfMac[];
extern uint8_t __xdata APmac[8];

extern uint8_t __xdata curImgSlot;
extern uint8_t __xdata currentChannel;

extern bool __xdata fastNextCheckin;

extern bool validateFWMagic(void);

extern bool validateMD5(uint32_t addr, uint16_t len) __reentrant;
extern uint8_t getEepromImageDataArgument(const uint8_t slot);
extern void initializeProto(void);

extern void drawImageFromEeprom(const uint8_t imgSlot);

#endif
