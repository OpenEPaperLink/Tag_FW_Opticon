#include "hal.h"
#include "dma.h"

#include "printf.h"

dmaConf __xdata dma[5];

// 0-1 SPI
// 2
// 3 radio rx
// 4 copy

void setupDMA(void) {
    for (uint8_t c = 0; c < 5; c++) {
        dma[c].irqmask = 0;
        dma[c].m8 = 0;
        dma[c].wordsize = 0;
        dma[c].trig = 0;
        dma[c].vlen = 0;
        dma[c].prio = 0;
        dma[c].srcinc = 0;
        dma[c].dstinc = 0;
    }

    // channel 4 for xdata copy
    dma[4].srcinc = 1;
    dma[4].dstinc = 1;
    dma[4].tmode = 1;

    // channel 0 for SPI eeprom read
    dma[0].srcinc = 0;
    dma[0].dstinc = 1;
    dma[0].trig = 16;  // URX event
    dma[0].tmode = 0;  // repeated single
    dma[0].prio = 2;   // high prio for the reading DMA
    SET_WORD(dma[0].src_h, dma[0].src_l, &X_U1DBUF);

    // channel 1 for SPI eeprom write
    dma[1].dstinc = 0;
    dma[1].trig = 17;  // UTX event
    dma[1].tmode = 0;
    dma[1].prio = 0;  // low prio
    SET_WORD(dma[1].dst_h, dma[1].dst_l, &X_U1DBUF);

    SET_WORD(DMA0CFGH, DMA0CFGL, &dma[0]);
    SET_WORD(DMA1CFGH, DMA1CFGL, &dma[1]);
}

void setupDMAChannel(uint8_t channel, uint8_t use) {
    static uint8_t __xdata curSetup[4] = {0, 0, 0, 0};
    if (curSetup[channel] == use) return;

    switch (use) {
        case DMA_SETUP_RF_WRITE:
            dma[channel].irqmask = 0;
            dma[channel].m8 = 0;
            dma[channel].wordsize = 0;
            dma[channel].vlen = 0;
            dma[channel].prio = 0;
            dma[channel].srcinc = 1;
            dma[channel].dstinc = 0;
            dma[channel].trig = 0;  // no trigger
            dma[channel].tmode = 1;
            SET_WORD(dma[channel].dst_h, dma[channel].dst_l, &X_RFD);
            break;
        case DMA_SETUP_RF_READ:
            dma[channel].irqmask = 0;
            dma[channel].m8 = 0;
            dma[channel].wordsize = 0;
            dma[channel].vlen = 0;
            dma[channel].prio = 0;
            dma[channel].srcinc = 0;
            dma[channel].dstinc = 1;
            dma[channel].trig = 0;  // no trigger
            dma[channel].tmode = 1;
            SET_WORD(dma[channel].src_h, dma[channel].src_l, &X_RFD);
            break;
        case DMA_SETUP_FLASH_WRITE:
            // setup channel 3 for writing to flash
            dma[channel].srcinc = 1;
            dma[channel].dstinc = 0;
            dma[channel].trig = 18;  //
            dma[channel].tmode = 0;  // repeated single
            dma[channel].prio = 2;   // high prio for the reading DMA
            dma[channel].irqmask = 0;
            dma[channel].m8 = 0;
            SET_WORD(dma[channel].dst_h, dma[channel].dst_l, &FWDATA);
    }

    curSetup[channel] = use;
}

void dmaMemCpy(void __xdata* dst, const void __xdata* src, uint16_t len) {
    SET_WORD(dma[4].src_h, dma[4].src_l, src);
    SET_WORD(dma[4].dst_h, dma[4].dst_l, dst);
    SET_WORD(dma[4].len_h, dma[4].len_l, len);
    DMAIRQ &= ~(1 << 4);
    DMAARM |= (1 << 4);
    DMAREQ |= (1 << 4);
    while ((DMAIRQ & (1 << 4)) != 0x10);
}

void dmaAsyncMemCpy(void __xdata* dst, const void __xdata* src, uint16_t len) {
    SET_WORD(dma[4].src_h, dma[4].src_l, src);
    SET_WORD(dma[4].dst_h, dma[4].dst_l, dst);
    SET_WORD(dma[4].len_h, dma[4].len_l, len);
    DMAIRQ &= ~(1 << 4);
    DMAARM |= (1 << 4);
    DMAREQ |= (1 << 4);
}

void dmaCodeCpy(void __xdata* dst, uint8_t __code* src, uint16_t len) {
    if ((uint16_t)src < 0x8000) {
        MEMCTR &= ~(0x07);
        src += 0x8000;
    } else {
        MEMCTR &= ~(0x07);
        MEMCTR |= FMAP & 0x07;
    }

    SET_WORD(dma[4].src_h, dma[4].src_l, src);
    SET_WORD(dma[4].dst_h, dma[4].dst_l, dst);
    SET_WORD(dma[4].len_h, dma[4].len_l, len);
    DMAIRQ &= ~(1 << 4);
    DMAARM |= (1 << 4);
    DMAREQ |= (1 << 4);
    while ((DMAIRQ & (1 << 4)) != 0x10);
}

void dmaMemSet(void __xdata* dst, const __xdata uint8_t c, uint16_t len) {
    dma[4].srcinc = 0;
    SET_WORD(dma[4].src_h, dma[4].src_l, &c);
    SET_WORD(dma[4].dst_h, dma[4].dst_l, dst);
    SET_WORD(dma[4].len_h, dma[4].len_l, len);
    DMAIRQ &= ~(1 << 4);
    DMAARM |= (1 << 4);
    DMAREQ |= (1 << 4);
    while ((DMAIRQ & (1 << 4)) != 0x10);
}

void dmaSPIRead(uint8_t __xdata* dst, uint16_t len) {
    // dma channel 0 for reading
    SET_WORD(dma[0].dst_h, dma[0].dst_l, dst);
    SET_WORD(dma[0].len_h, dma[0].len_l, len);
    DMAIRQ &= ~(1 << 0);

    // 1 for writing dummy bytes
    uint8_t __xdata dummy = 0;
    SET_WORD(dma[1].src_h, dma[1].src_l, &dummy);
    dma[1].srcinc = 0;
    SET_WORD(dma[1].len_h, dma[1].len_l, len);
    DMAIRQ &= ~(1 << 1);

    // load channel 0 and 1 configs
    DMAARM |= (1 << 0);
    DMAARM |= (1 << 1);

    DMAREQ |= (1 << 1);        // initiate first transfer, DMA controller will take over
    while (!(DMAIRQ & 0x01));  // wait until the RX DMA channel is satisfied
}

void dmaSPIWrite(uint8_t __xdata* src, uint16_t len) {
    SET_WORD(dma[1].src_h, dma[1].src_l, src);
    dma[1].srcinc = 1;
    SET_WORD(dma[1].len_h, dma[1].len_l, len);
    DMAIRQ &= ~(1 << 1);
    // arm config
    DMAARM |= (1 << 1);
    // kick off DMA transfer
    DMAREQ |= (1 << 1);
}
