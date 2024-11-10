#ifndef DRAWING_H
#define DRAWING_H

#include "stdint.h"
#include "stdbool.h"

struct drawItem {
    uint16_t xPos;  // multiple's of 8
    uint16_t yPos;
    uint8_t xSize;  // in bytes
    uint8_t ySize;  // multiple's of 8 when rotated
    uint8_t color;
    bool rotate;
    uint8_t __xdata* buffer;
    uint32_t addr;
    uint8_t scale;
    uint8_t type;
};

#define DI_TYPE_REGULAR_BUFFER 0
#define DI_TYPE_EEPROM_1BPP 1
#define DI_TYPE_EEPROM_2BPP 2

//void testDraw();
void getLine(uint16_t pos, uint8_t color, uint8_t*  buffer) __reentrant;
void clearDrawList(void);
struct drawItem __xdata* newDrawItem(void) __reentrant;

void epdPrintf(const char __code* fmt, ...);
void epdPrintBegin(void) ;

void epdSetPos(uint16_t x, uint16_t y, bool rotate);
void epdSetScale(uint8_t scale);
void epdSetColor(uint8_t color);
void epdDrawImage(uint8_t __code* image)__reentrant;

void drawImageAtAddress(uint32_t addr) __reentrant;

#endif
