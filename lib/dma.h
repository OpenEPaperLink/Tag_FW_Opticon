#ifndef DMA_H
#define DMA_H

#include "stdint.h"

typedef struct {
    uint8_t src_h;
    uint8_t src_l;
    
    uint8_t dst_h;
    uint8_t dst_l;

    uint8_t len_h : 5;  // High byte of fixed length
    uint8_t vlen : 3;  // Length configuration

    uint8_t len_l : 8;  // Low byte of fixed length

    uint8_t trig : 5;      // DMA trigger; SPI RX/TX
    uint8_t tmode : 2;     // DMA trigger mode (e.g. single or repeated)
    uint8_t wordsize : 1;  // Number of bytes per transfer element

    uint8_t prio : 2;  // The DMA memory access priority
    uint8_t m8 : 1;        // Number of desired bit transfers in byte mode
    uint8_t irqmask : 1;   // DMA interrupt mask
    uint8_t dstinc : 2;   // Number of destination address increments
    uint8_t srcinc : 2;    // Number of source address increments
} dmaConf;

extern dmaConf __xdata dma[5];

#define DMA_SETUP_RF_WRITE 2
#define DMA_SETUP_RF_READ 3
#define DMA_SETUP_FLASH_WRITE 4

void setupDMAChannel(uint8_t channel, uint8_t use);

void dmaMemCpy(void __xdata* dst, const void __xdata*, uint16_t len);
void dmaAsyncMemCpy(void __xdata* dst, const void __xdata* src, uint16_t len);
void dmaCodeCpy(void __xdata* dst, uint8_t __code* src, uint16_t len);
void dmaMemSet(void __xdata* dst, const __xdata uint8_t c, uint16_t len);
void setupDMA(void);
void dmaSPIRead(uint8_t __xdata* dst, uint16_t len);

#endif