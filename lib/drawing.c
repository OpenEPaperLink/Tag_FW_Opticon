#include "hal.h"

#include <stdlib.h>
#include "asmUtil.h"
#include <string.h>
#include <stdarg.h>

#include "drawing.h"
#include "printf.h"
#include "font.h"
#include "dma.h"

#include "powermgt.h"

#include "screen.h"

#include "userinterface.h"

#define __packed
#include "eeprom.h"
#include "../shared/oepl-proto.h"
#include "../shared/oepl-definitions.h"

#define WHITESPACE 4
#define INTER_CHAR 1
#define FONTWIDTH_BITS 16
#define FONTWIDTH_BYTES 2
#define FONTHEIGHT 16

__xdata struct drawItem* di;
__xdata struct drawItem* drawItemList = NULL;
__xdata uint8_t drawItemCount = 0;

void reverse_bytes(__xdata uint8_t* array, size_t length) {
    size_t i;
    uint8_t temp;

    for (i = 0; i < length / 2; i++) {
        // Swap the bytes at positions i and (length - i - 1)
        temp = array[i];
        array[i] = array[length - i - 1];
        array[length - i - 1] = temp;
    }
}

void invert_reverse_bytes(__xdata uint8_t* array, size_t length) {
    size_t i;
    uint8_t temp;

    for (i = 0; i < length / 2; i++) {
        // Swap the bytes at positions i and (length - i - 1)
        temp = array[i];
        array[i] = 0xFF ^ array[length - i - 1];
        array[length - i - 1] = 0xFF ^ temp;
    }
}

void invert_bytes(__xdata uint8_t* array, size_t length) {
    for (uint8_t i = 0; i < length; i++) {
        array[i] = 0xFF ^ array[i];
    }
}

void getLine(uint16_t pos, uint8_t color, uint8_t* buffer) __reentrant {
    struct drawItem di;
    for (uint8_t c = 0; c < drawItemCount; c++) {
        memcpy(&di, &drawItemList[c], sizeof(struct drawItem));
        if (di.color == color) {
            // draw buffers directly
            if (di.rotate) {
                // draw in Y direction
                if (pos >= di.yPos)
                    if (pos < di.yPos + (di.scale * 8 * di.xSize)) {
                        uint16_t xOffset = (pos - di.yPos) / di.scale;
                        for (uint8_t x = 0; x < (di.ySize * di.scale); x++) {
                            if (di.buffer[(xOffset / 8) + (di.xSize * (x / di.scale))] & (0x80 >> (xOffset % 8))) {
                                uint16_t outOffset = di.xPos + x;
                                buffer[outOffset / 8] |= 1 << (outOffset % 8);
                            }
                        }
                    }

            } else {
                // draw in X direction
                if (pos >= di.yPos)
                    if (pos < di.yPos + (di.ySize * di.scale)) {
                        uint8_t yIndex = ((di.ySize * di.scale) - 1) - ((pos - di.yPos));
                        for (uint16_t i = 0; i < (di.xSize * 8 * di.scale); i++) {
                            if (di.buffer[((i / 8) / di.scale) + ((yIndex / di.scale) * di.xSize)] & (0x80 >> (i / di.scale) % 8)) {
                                buffer[(i / 8) + (di.xPos / 8)] |= 1 << i % 8;
                            }
                        }
                    }
            }
        } else {
            // eeprom-based drawing
            if (di.type > 0) {
                if (di.type == DI_TYPE_EEPROM_2BPP) {
                    if (color == 0) {
                        // b/w info
                        // read red data into buff1
                        uint32_t addr = di.addr;
                        addr += sizeof(struct EepromImageHeader);
                        addr += (SCREEN_WIDTH / 8) * pos;
                        addr += (SCREEN_WIDTH / 8) * SCREEN_HEIGHT;
                        __xdata uint8_t* buff1 = malloc(SCREEN_WIDTH / 8);
                        eepromRead(addr, (__xdata uint8_t*)buff1, (SCREEN_WIDTH / 8));

                        // read b/w data into buffer
                        addr = di.addr;
                        addr += sizeof(struct EepromImageHeader);
                        addr += ((SCREEN_WIDTH / 8) * pos);
                        eepromRead(addr, (__xdata uint8_t*)buffer, (SCREEN_WIDTH / 8));

                        invert_bytes((__xdata uint8_t*)buff1, (SCREEN_WIDTH / 8));

                        // & these buffers to obtain black info
                        for (uint8_t c = 0; c < (SCREEN_WIDTH / 8); c++) {
                            buffer[c] &= buff1[c];
                        }
                        free(buff1);
                        reverse_bytes((__xdata uint8_t*)buffer, (SCREEN_WIDTH / 8));
                    } else if (color == 1) {
                        // read red data into buff1
                        uint32_t addr = di.addr;
                        addr += sizeof(struct EepromImageHeader);
                        addr += (SCREEN_WIDTH / 8) * pos;
                        addr += (SCREEN_WIDTH / 8) * SCREEN_HEIGHT;
                        __xdata uint8_t* buff1 = malloc(SCREEN_WIDTH / 8);
                        eepromRead(addr, (__xdata uint8_t*)buff1, (SCREEN_WIDTH / 8));

                        // read b/w data into buffer
                        addr = di.addr;
                        addr += sizeof(struct EepromImageHeader);
                        addr += ((SCREEN_WIDTH / 8) * pos);
                        eepromRead(addr, (__xdata uint8_t*)buffer, (SCREEN_WIDTH / 8));

                        invert_bytes((__xdata uint8_t*)buffer, (SCREEN_WIDTH / 8));

                        // & these buffers to obtain red info
                        for (uint8_t c = 0; c < (SCREEN_WIDTH / 8); c++) {
                            buffer[c] &= buff1[c];
                        }

                        free(buff1);
                        reverse_bytes((__xdata uint8_t*)buffer, (SCREEN_WIDTH / 8));
                    } else if (color == 2) {
                        // yellow layer

                        // read red data into buff1
                        uint32_t addr = di.addr;
                        addr += sizeof(struct EepromImageHeader);
                        addr += (SCREEN_WIDTH / 8) * pos;
                        addr += (SCREEN_WIDTH / 8) * SCREEN_HEIGHT;
                        __xdata uint8_t* buff1 = malloc(SCREEN_WIDTH / 8);
                        eepromRead(addr, (__xdata uint8_t*)buff1, (SCREEN_WIDTH / 8));

                        // read b/w data into buffer
                        addr = di.addr;
                        addr += sizeof(struct EepromImageHeader);
                        addr += ((SCREEN_WIDTH / 8) * pos);
                        eepromRead(addr, (__xdata uint8_t*)buffer, (SCREEN_WIDTH / 8));

                        // & these buffers to obtain yellow info
                        for (uint8_t c = 0; c < (SCREEN_WIDTH / 8); c++) {
                            buffer[c] &= buff1[c];
                        }
                        free(buff1);
                        reverse_bytes((__xdata uint8_t*)buffer, (SCREEN_WIDTH / 8));
                    }
                } else if (di.type == DI_TYPE_EEPROM_1BPP) {
                    if (color == 0) {
                        uint32_t addr = di.addr;
                        addr += sizeof(struct EepromImageHeader);
                        addr += ((SCREEN_WIDTH / 8) * pos);
                        eepromRead(addr, (__xdata uint8_t*)buffer, (SCREEN_WIDTH / 8));
                        reverse_bytes((__xdata uint8_t*)buffer, (SCREEN_WIDTH / 8));
                    }
                }
            }
        }
    }
}

void clearDrawList(void) {
    for (uint8_t c = 0; c < drawItemCount; c++) {
        if (drawItemList[c].buffer) free(drawItemList[c].buffer);
    }
    if (drawItemList) free(drawItemList);
    drawItemList = NULL;
    drawItemCount = 0;
}

struct drawItem __xdata* newDrawItem(void) __reentrant {
    if (drawItemList) {
        drawItemCount++;
        struct drawItem __xdata* new = malloc(drawItemCount * sizeof(struct drawItem));
        new->type = DI_TYPE_REGULAR_BUFFER;
        memcpy(new, drawItemList, (drawItemCount - 1) * sizeof(struct drawItem));
        free(drawItemList);
        drawItemList = new;
    } else {
        drawItemList = (struct drawItem __xdata*)malloc(sizeof(struct drawItem));
        drawItemCount++;
    }
    return &drawItemList[(drawItemCount - 1)];
}

void epdPrintBegin(void) {
    di = newDrawItem();
    di->scale = 1;
    di->color = 0;
    di->xPos = 8;
    di->yPos = 8;
    di->rotate = 0;
}

void epdSetPos(uint16_t x, uint16_t y, bool rotate) {
    di->xPos = x;
    di->yPos = y;
    di->rotate = rotate;
}

void epdSetScale(uint8_t scale) {
    di->scale = scale;
}

void epdSetColor(uint8_t color) {
    di->color = color;
}

void epdDrawImage(uint8_t __code* image) __reentrant {
    di = newDrawItem();

    di->scale = 2;
    di->color = 0;
    di->xPos = 64;
    di->yPos = 4;
    di->rotate = 2;

    di->xSize = image[0] / 8;
    di->ySize = image[2];
    di->buffer = malloc(di->xSize * di->ySize);
    memcpy(di->buffer, image + 4, (di->xSize * di->ySize));
}
void epdPrintf(const char __code* fmt, ...) {
    uint8_t* textbuf = malloc(48);
    va_list vl;
    va_start(vl, fmt);
    epdspr((char __xdata*)textbuf, fmt, vl);
    va_end(vl);

    // allocate temp character buffer
    uint8_t* buf = malloc((FONTWIDTH_BYTES + 1) * FONTHEIGHT);

    // calculate width of the textbox
    uint8_t charindex = 0;
    uint16_t textpos = 0;
    char ch;
    while (ch = textbuf[charindex]) {
        if (ch == 0x20) {
            textpos += WHITESPACE;
        } else {
            uint8_t charwidth = font[ch - 0x20][0];
            uint8_t thiswidth = FONTWIDTH_BITS - (charwidth & 0x0F);
            thiswidth -= (charwidth >> 4);
            textpos += thiswidth;
            textpos += INTER_CHAR;
        }
        charindex++;
    }
    // added after every character, but not needed after the last one
    textpos -= INTER_CHAR;
    uint8_t widthBytes = textpos / 8;
    if (textpos % 8) widthBytes++;

    // allocate textbox pixelbuffer
    di->buffer = malloc(widthBytes * FONTHEIGHT);
    if (!di->buffer) {
        pr("Malloc failed!\n");
        return;
    }
    memset(di->buffer, 0x00, widthBytes * FONTHEIGHT);
    di->xSize = widthBytes;
    di->ySize = FONTHEIGHT;

    // iterate over all characters, render them to the buffer
    charindex = 0;
    textpos = 0;
    while (ch = textbuf[charindex]) {
        if (ch == 0x20) {
            textpos += WHITESPACE;
        } else {
            // get alignment data for character
            uint8_t charwidth = font[ch - 0x20][0];
            uint8_t thiswidth = FONTWIDTH_BITS - (charwidth & 0x0F);
            uint8_t left = (charwidth >> 4);
            thiswidth -= left;

            // copy character to temp buffer
            for (uint8_t row = 0; row < FONTHEIGHT; row++) {
                buf[0 + (row * (FONTWIDTH_BYTES + 1))] = font[ch - 0x20][(row * FONTWIDTH_BYTES) + 2];
                buf[1 + (row * (FONTWIDTH_BYTES + 1))] = font[ch - 0x20][(row * FONTWIDTH_BYTES) + 1];
                buf[2 + (row * (FONTWIDTH_BYTES + 1))] = 0x00;
            }

            // align character to the right position in the buffer
            if (left != (textpos % 8)) {
                if (left < (textpos % 8)) {
                    uint8_t shift = (textpos % 8) - left;
                    if (shift >= 8) {
                        shift %= 8;
                        for (uint8_t c = 0; c < (FONTHEIGHT * 3); c += 3) {
                            buf[2 + c] = buf[1 + c];
                            buf[1 + c] = buf[0 + c];
                            buf[0 + c] = 0x00;
                        }
                    }
                    for (uint8_t c = 0; c < (FONTHEIGHT * 3); c += 3) {
                        buf[2 + c] = (buf[2 + c] >> shift) | (buf[1 + c] << (8 - shift));
                        buf[1 + c] = (buf[1 + c] >> shift) | (buf[0 + c] << (8 - shift));
                        buf[0 + c] >>= shift;
                    }
                } else {
                    uint8_t shift = left - (textpos % 8);
                    if (shift >= 8) {
                        shift %= 8;
                        for (uint8_t c = 0; c < (FONTHEIGHT * 3); c += 3) {
                            buf[0 + c] = buf[1 + c];
                            buf[1 + c] = buf[2 + c];
                            buf[2 + c] = 0x00;
                        }
                    }
                    for (uint8_t c = 0; c < (FONTHEIGHT * 3); c += 3) {
                        buf[0 + c] = (buf[0 + c] << shift) | (buf[1 + c] >> (8 - shift));
                        buf[1 + c] = (buf[1 + c] << shift) | (buf[2 + c] >> (8 - shift));
                        buf[2 + c] <<= shift;
                    }
                }
            }

            // copy character onto textbox buffer
            for (uint8_t i = 0; i < FONTHEIGHT; i++) {
                uint8_t offset = (textpos / 8);
                di->buffer[2 + offset + (i * widthBytes)] |= buf[2 + (i * (FONTWIDTH_BYTES + 1))];
                // danger! this -will- write outside of the buffer area on the last character, but it's okay since it OR's the memory with 0x00 (generally speaking)
                di->buffer[1 + offset + (i * widthBytes)] |= buf[1 + (i * (FONTWIDTH_BYTES + 1))];
                di->buffer[0 + offset + (i * widthBytes)] |= buf[(i * (FONTWIDTH_BYTES + 1))];
            }

            textpos += thiswidth;
            textpos += INTER_CHAR;
        }
        charindex++;
    }

    // cleanup
    free(buf);
    free(textbuf);
}

void drawImageAtAddress(uint32_t addr) __reentrant {
    di = newDrawItem();
    powerUp(INIT_EEPROM);
    __xdata struct EepromImageHeader* eih = malloc(sizeof(struct EepromImageHeader));
    eepromRead(addr, eih, sizeof(struct EepromImageHeader));
    if (eih->dataType == DATATYPE_IMG_RAW_1BPP) {
        di->type = DI_TYPE_EEPROM_1BPP;
    } else if (eih->dataType == DATATYPE_IMG_RAW_2BPP) {
        di->type = DI_TYPE_EEPROM_2BPP;
    }
    free(eih);

    di->buffer = NULL;
    di->addr = addr;
    di->color = 0xFE;
    addOverlay();
    epdDisplay();
}