
#include "hal.h"

#include <stdint.h>

#include "radioconst.h"
#include <stdlib.h>
#include <stdbool.h>
#include "meinradio.h"
#include <string.h>
#include "printf.h"

#include "delay.h"
#include "proto.h"

#include "dma.h"
#define __packed

#include "../shared/oepl-proto.h"
#include "../shared/oepl-definitions.h"
#include "eeprom.h"
#include "powermgt.h"
static uint8_t __xdata rf_flags;
int8_t __xdata lastRSSI;
int8_t __xdata lastLQI;

// Ich lass mich in den Äther saugen

// This is heavily inspired on Contiki's radio interface, with a bunch of other stuff added

void radioOn(void) {
    if (!(rf_flags & RX_ACTIVE)) {
        CC2530_CSP_ISFLUSHRX();
        CC2530_CSP_ISRXON();
        rf_flags |= RX_ACTIVE;
    }
    //delay_us(200); // changed
}

//#include "ota.y"

void radioOff(void) {
    if (rf_flags & RX_ACTIVE) {
        CC2530_CSP_ISRFOFF();
        CC2530_CSP_ISFLUSHRX();
        rf_flags &= ~RX_ACTIVE;
    }
}
static void radioSetPanID(uint16_t pan) {
    PAN_ID0 = pan & 0xFF;
    PAN_ID1 = pan >> 8;
}
void radioSetChannel(uint8_t channel) {
    if ((channel < CC2530_RF_CHANNEL_MIN) || (channel > CC2530_RF_CHANNEL_MAX)) {
        return;
    }
    /* Changes to FREQCTRL take effect after the next recalibration */
    radioOff();
    FREQCTRL = (CC2530_RF_CHANNEL_MIN + (channel - CC2530_RF_CHANNEL_MIN) * CC2530_RF_CHANNEL_SPACING);
    radioOn();
}

inline void radioRxFlush(void) {
    CC2530_CSP_ISFLUSHRX();
}

int8_t radioRx(__xdata void* buf) {
    if (!(FSMSTAT1 & FSMSTAT1_FIFOP)) {
        return COMMS_RX_ERR_NO_PACKETS;
    }

    uint8_t len;
    uint8_t crc_corr;

    len = RFD;

    if (len > CC2530_RF_MAX_PACKET_LEN) {
        /* Oops, we must be out of sync. */
        pr("RF: bad sync\n");
        CC2530_CSP_ISFLUSHRX();
        return 0;
    }

    if (len <= CC2530_RF_MIN_PACKET_LEN) {
        pr("RF: too short\n");
        CC2530_CSP_ISFLUSHRX();
        return 0;
    }

    len -= CHECKSUM_LEN;

    // setup DMA
    setupDMAChannel(3, DMA_SETUP_RF_READ);
    SET_WORD(dma[3].dst_h, dma[3].dst_l, buf);
    SET_WORD(dma[3].len_h, dma[3].len_l, len);
    DMAIRQ &= ~(1 << 3);
    DMAARM |= (1 << 3);

    // start DMA
    DMAREQ |= (1 << 3);
    while ((DMAIRQ & (1 << 3)) != 0x08);

    /* Read the RSSI and CRC/Corr bytes */
    lastRSSI = ((int8_t)RFD) - RSSI_OFFSET;
    crc_corr = RFD;

    /* MS bit CRC OK/Not OK, 7 LS Bits, Correlation value */
    if (crc_corr & CRC_BIT_MASK) {
        // packetbuf_set_attr(PACKETBUF_ATTR_RSSI, rssi);
        lastLQI = 10;
        // packetbuf_set_attr(PACKETBUF_ATTR_LINK_QUALITY, crc_corr & LQI_BIT_MASK);
    } else {
        //pr("CRC Fail!");
        //CC2530_CSP_ISFLUSHRX();
        return 0;
    }

    /* If FIFOP==1 and FIFO==0 then we had a FIFO overflow at some point. */
    if ((FSMSTAT1 & (FSMSTAT1_FIFO | FSMSTAT1_FIFOP)) == FSMSTAT1_FIFOP) {
        /*
         * If we reach here means that there might be more intact packets in the
         * FIFO despite the overflow. This can happen with bursts of small packets.
         *
         * Only flush if the FIFO is actually empty. If not, then next pass we will
         * pick up one more packet or flush due to an error.
         */
        if (!RXFIFOCNT) {
            CC2530_CSP_ISFLUSHRX();
        }
    }

    return (len);
}
void radioTx(__xdata uint8_t* buff) {
    while (FSMSTAT1 & FSMSTAT1_TX_ACTIVE);

    if ((rf_flags & RX_ACTIVE) == 0) {
        radioOn();
    }

   // delay_ms(20);
     CC2530_CSP_ISFLUSHTX();
    //delay_ms(20);  // changed

    setupDMAChannel(2, DMA_SETUP_RF_WRITE);
    SET_WORD(dma[2].src_h, dma[2].src_l, buff);
    SET_WORD(dma[2].len_h, dma[2].len_l, *buff + 1);
    DMAIRQ &= ~(1 << 2);
    DMAARM |= (1 << 2);

    DMAREQ |= (1 << 2);
    while ((DMAIRQ & (1 << 2)) != 0x04);

    if (!(FSMSTAT1 & FSMSTAT1_CCA)) {
        pr("CCA fail!\n");
        return;  // RADIO_TX_COLLISION;
    }

    if (FSMSTAT1 & FSMSTAT1_SFD) {
        pr("TX fail, in RX\n");
        return;  // RADIO_TX_COLLISION;
    }

    CC2530_CSP_ISTXON();

    uint8_t __xdata counter = 0;
    while (!(FSMSTAT1 & FSMSTAT1_TX_ACTIVE) && (counter++ < 3)) {
        delay_us(60);
    }

    if (!(FSMSTAT1 & FSMSTAT1_TX_ACTIVE)) {
        CC2530_CSP_ISFLUSHTX();
        // ret = RADIO_TX_ERR;
        pr("TX error\n");
    } else {
        /* Wait for the transmission to finish */
        while (FSMSTAT1 & FSMSTAT1_TX_ACTIVE);
        // ret = RADIO_TX_OK;
    }
}

void radioRxEnable(bool renable) {
    if (renable) {
        radioOn();
    } else {
        radioOff();
    }
}

void radioInit(void) {
    if (rf_flags & RF_ON) {
        for (uint8_t c = 0; c < 8; c++) {
           *((uint8_t*)&EXT_ADDR0 + c) = mSelfMac[c];
        }
        return;
    }

    setupDMAChannel(3, DMA_SETUP_RF_READ);

    RFST = 0xE3;  // RFST Command Strobe: IDLE
    // 2. Clear RX buffer and any interrupt flags
    RFIRQF1 = 0;  // Clear all RF interrupt flags

    for (uint8_t c = 0; c < 8; c++) {
        *((uint8_t*)&EXT_ADDR0 + c) = mSelfMac[c];
    }

    SHORT_ADDR0 = EXT_ADDR6;
    SHORT_ADDR1 = EXT_ADDR7;

    // memcpy(EXT_ADDR0, mac, 8);
    /* low power settings:
        RXCTRL = 0x00;
        FSCTRL = 0x50;
    */
    RXCTRL = 0x3F;
    FSCTRL = 0x55;

    // CCA therehold
    CCACTRL0 = 0xF8;

    /*
     * According to the user guide, these registers must be updated from their
     * defaults for optimal performance
     *
     * Table 23-6, Sec. 23.15.1, p. 259
     */
    TXFILTCFG = 0x09; /* TX anti-aliasing filter */
    AGCCTRL1 = 0x15;  /* AGC target value */
    FSCAL1 = 0x00;    /* Reduce the VCO leakage */

    /* Auto ACKs and CRC calculation, default RX and TX modes with FIFOs */
    FRMCTRL0 = FRMCTRL0_AUTOCRC;
    FRMFILT0 |= FRMFILT0_FRAME_FILTER_EN;
    // FRMCTRL0 |= FRMCTRL0_AUTOACK;

    /* Disable source address matching and autopend */
    SRCMATCH = 0; /* investigate */

    CC2530_CSP_ISFLUSHRX();  // new

    /* MAX FIFOP threshold */
    FIFOPCTRL = 127;
    TXPOWER = 0xD5;
    rf_flags |= RF_ON;
    radioSetPanID(0x4447);
    radioSetChannel(currentChannel);
}