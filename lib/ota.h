#ifndef BOOTLOADER_H
#define BOOTLOADER_H


void programFlashFromEEPROM(uint32_t address, uint8_t pages) __reentrant;
void programFlashCompilerSatisfier(void);
#endif