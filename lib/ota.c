#include "hal.h"
#include "printf.h"

#include "dma.h"
#include "eeprom.h"

#include <stdlib.h>
#include <string.h>

#include <stdint.h>

#include "powermgt.h"

#define LED 0

#define FLASH_BLOCKSIZE 1024

// this func gets copied to RAM, and is used to erase/program the flash from SPI EEPROM
__xdata uint8_t *srcBuf;
void RAMFuncProgramFlashFromEEPROM(uint8_t maxPage) __reentrant {
    //__xdata uint8_t *srcBuf = malloc(FLASH_BLOCKSIZE);
    EA = 0;
    SET_WORD(dma[0].dst_h, dma[0].dst_l, srcBuf);
    SET_WORD(dma[3].src_h, dma[3].src_l, srcBuf);
    for (uint8_t page = 0; page < maxPage; page += 1) {
        WDCTL = 0xA8;
        WDCTL = 0x58;
        // clear config 0/1 IRQ flags
        DMAIRQ &= ~(3);
        for (uint16_t c = 65535; c > 0; c--);

        // load channel 0 and 1 configs
        DMAARM |= (1 << 0);
        DMAARM |= (1 << 1);
        DMAREQ |= (1 << 1);  // initiate first transfer, DMA controller will take over

        while (!(DMAIRQ & 0x01));  // wait until the RX DMA channel is satisfied

        // we'll write entire pages in one shot
        FADDRH = page;
        DMAARM |= (1 << 3);
        while (!(DMAARM & (1 << 3)));
        P1 |= (1 << LED);

        FCTL |= FCTL_WRITE | FCTL_ERASE;  // start flash write
        P1 &= ~(1 << LED);
        while (!(DMAIRQ & (1 << 3)));

        // clear DMA channel 3 IRQ
        DMAIRQ &= ~(1 << 3);
    }

    // force WDT reset
    WDCTL = 0x0B;
    while (1);
}

// Function end marker
void RAMFuncEndMarker(void) {}

void programFlashFromEEPROM(uint32_t address, uint8_t pages) __reentrant {
    // setupDMA();
    powerUp(INIT_EEPROM);
    void (*RAMFuncP)(uint8_t);
    uint8_t *ram_memory;
    uint16_t RAMFuncSize;

    // Calculate the size of the function using the difference between labels
    RAMFuncSize = (uint16_t)RAMFuncEndMarker - (uint16_t)RAMFuncProgramFlashFromEEPROM;

    // Allocate memory for the function in RAM
    srcBuf = malloc(FLASH_BLOCKSIZE);
    ram_memory = (uint8_t *)malloc(RAMFuncSize);

    eepromReadStart(address);

    DMAARM |= 0xFF;  // disable all existing DMA transfers/configs

    // setup channel 3 for writing to flash
    dma[3].srcinc = 1;
    dma[3].dstinc = 0;
    dma[3].trig = 18;  //
    dma[3].tmode = 0;  // repeated single
    dma[3].prio = 2;   // high prio for the reading DMA
    dma[3].irqmask = 0;
    dma[3].m8 = 0;
    SET_WORD(dma[3].dst_h, dma[3].dst_l, &FWDATA);
    SET_WORD(dma[3].len_h, dma[3].len_l, FLASH_BLOCKSIZE);
    // dma channel 0 for reading
    SET_WORD(dma[0].len_h, dma[0].len_l, FLASH_BLOCKSIZE);
    // 1 for writing dummy bytes
    uint8_t dummy = 0;
    SET_WORD(dma[1].src_h, dma[1].src_l, &dummy);
    dma[1].srcinc = 0;
    SET_WORD(dma[1].len_h, dma[1].len_l, FLASH_BLOCKSIZE);
    FADDRL = 0;

    // Copy the function to the allocated RAM space
    memcpy(ram_memory, (uint8_t *)RAMFuncProgramFlashFromEEPROM, RAMFuncSize);

    // Map the RAM into the second flash bank using the MEMCTR register
    MEMCTR |= 0x08;

    // Calculate the address to jump to
    RAMFuncP = (void (*)(uint8_t))(0x8000 + ((unsigned int)ram_memory));

    // Jump to the function in RAM
    RAMFuncP(pages);
    // <- we never get here :(
}
void programFlashCompilerSatisfier(void) {
}