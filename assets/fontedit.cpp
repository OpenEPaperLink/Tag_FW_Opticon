#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
// #include <iostream>
#include <string.h>
#include <zlib.h>

#define __code
#include "10x16_horizontal_MSB_1.h"

void printChar(uint8_t ch) {
    printf("0123456776543210 - %c\n", ch);
    for (uint8_t height = 0; height < 16; height++) {
        uint16_t row = (font[ch][1 + (height * 2)] << 8) | font[ch][(height * 2)];
        for (uint8_t i = 0; i < 16; i++) {
            if (row & 0x8000) {
                printf("#");
            } else {
                printf(".");
            }
            row <<= 1;
        }
        printf(" - 0x%02X 0x%02X \n", font[ch][1 + (height * 2)], font[ch][(height * 2)]);
    }
}

int main() {
    for (uint8_t c = 0x20; c < 0x7F; c++) {
       // printChar(c);
        uint8_t leftOffset = 15;
        uint8_t rightOffset = 15;
        for (uint16_t offset = 0; offset < 15; offset++) {
            for (uint8_t height = 0; height < 16; height++) {
                if (leftOffset != 15) break;
                uint16_t row = (font[c][1 + (height * 2)] << 8) | font[c][(height * 2)];
                if ((row << offset) & 0x8000) {
                    leftOffset = offset;
                    break;
                }
            }
        }

        for (uint16_t offset = 0; offset < 15; offset++) {
            for (uint8_t height = 0; height < 16; height++) {
                if (rightOffset != 15) break;
                uint16_t row = (font[c][1 + (height * 2)] << 8) | font[c][(height * 2)];
                if ((row >> offset) & 0x0001) {
                    rightOffset = offset;
                    break;
                }
            }
        }
        uint8_t offset = 0;

        offset |= (leftOffset << 4);
        offset |= rightOffset & 0x0F;
        printf("{0x%02X", offset);
        for (uint8_t d = 0; d < 32; d++) {
            printf(",0x%02X", font[c][d]);
        }
        printf("}, // 0x%02X - `%c`\n", c, c);
    }
}
