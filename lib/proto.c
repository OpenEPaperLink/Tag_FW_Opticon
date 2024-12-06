#include "hal.h"
#include <stdint.h>
#include <stdbool.h>

#include "asmUtil.h"

#define __packed
#include "../shared/oepl-proto.h"
#include "../shared/oepl-definitions.h"

#include "delay.h"

#include "proto.h"

#include <string.h>
#include <stdlib.h>
#include "printf.h"
#include "meinradio.h"

#include "md5.h"

#include "tagprofile.h"

#include "eeprom.h"
#include "drawing.h"
#include "powermgt.h"

#include "settings.h"

#include "ota.h"

#include "wdt.h"

#include "screen.h"
#include "g5/g5dec.h"

// stuff we need to keep track of related to the network/AP
uint8_t __xdata APmac[8] = {0};
uint16_t __xdata APsrcPan = 0;
uint8_t __xdata mSelfMac[8];  // = {0};//{0x9A, 0xB1, 0x49, 0xB6, 0x77, 0x7E, 0x00, 0x00};}
static uint8_t __xdata seq = 0;
uint8_t __xdata currentChannel = 0;

// download-stuff
static struct blockRequest __xdata curBlock = {0};  // used by the block-requester, contains the next request that we'll send
static uint8_t __xdata curDispDataVer[8] = {0};
static struct AvailDataInfo __xdata xferDataInfo = {0};  // holds the AvailDataInfo during the transfer
#define BLOCK_TRANSFER_ATTEMPTS 5

static uint8_t __xdata xferImgSlot = 0xFF;  // holds current transfer slot in progress
uint8_t __xdata curImgSlot = 0xFF;          // currently shown image
static uint32_t __xdata curHighSlotId = 0;  // current highest ID, will be incremented before getting written to a new slot
static uint8_t __xdata nextImgSlot = 0;     // next slot in sequence for writing
static uint8_t __xdata imgSlots = 0;
static uint32_t __xdata eeSize = 0;

#define OTA_UPDATE_SIZE 0x10000

// determines if the tagAssociated loop in main.c performs a rapid next checkin
bool __xdata fastNextCheckin = false;

// other stuff we shouldn't have to put here...
static uint32_t __xdata markerValid = EEPROM_IMG_VALID;

extern void executeCommand(uint8_t cmd);

static uint8_t __xdata getPacketType(const __xdata void *buffer) {
    const __xdata struct MacFcs *fcs = buffer;
    if ((fcs->frameType == 1) && (fcs->destAddrType == 2) && (fcs->srcAddrType == 3) && (fcs->panIdCompressed == 0)) {
        // broadcast frame
        uint8_t __xdata type = ((uint8_t *)buffer)[sizeof(struct MacFrameBcast)];
        return type;
    } else if ((fcs->frameType == 1) && (fcs->destAddrType == 3) && (fcs->srcAddrType == 3) && (fcs->panIdCompressed == 1)) {
        // normal frame
        uint8_t __xdata type = ((uint8_t *)buffer)[sizeof(struct MacFrameNormal)];
        return type;
    }
    return 0;
}
static bool pktIsUnicast(const __xdata void *buffer) {
    const __xdata struct MacFcs *fcs = buffer;
    if ((fcs->frameType == 1) && (fcs->destAddrType == 2) && (fcs->srcAddrType == 3) && (fcs->panIdCompressed == 0)) {
        return false;
    } else if ((fcs->frameType == 1) && (fcs->destAddrType == 3) && (fcs->srcAddrType == 3) && (fcs->panIdCompressed == 1)) {
        // normal frame
        return true;
    }
    // unknown type...
    return false;
}
static bool checkCRC(const void *p, const uint8_t len) {
    uint8_t total = 0;
    for (uint8_t c = 1; c < len; c++) {
        total += ((uint8_t *)p)[c];
    }
    // pr("CRC: rx %d, calc %d\n", ((uint8_t *)p)[0], total);
    return ((uint8_t *)p)[0] == total;
}
static void addCRC(void *p, const uint8_t len) {
    uint8_t total = 0;
    for (uint8_t c = 1; c < len; c++) {
        total += ((uint8_t *)p)[c];
    }
    ((uint8_t *)p)[0] = total;
}

static void sendPing(void) {
    __xdata uint8_t *outBuffer = malloc(32);
    __xdata struct MacFrameBcast *txframe = (struct MacFrameBcast *)(outBuffer + 1);
    memset(outBuffer, 0, sizeof(struct MacFrameBcast) + 2 + 4);
    outBuffer[0] = sizeof(struct MacFrameBcast) + 1 + 2;
    outBuffer[sizeof(struct MacFrameBcast) + 1] = PKT_PING;
    memcpy(txframe->src, mSelfMac, 8);
    txframe->fcs.frameType = 1;
    txframe->fcs.ackReqd = 0;
    txframe->fcs.destAddrType = 2;
    txframe->fcs.srcAddrType = 3;
    txframe->seq = seq++;
    txframe->dstPan = PROTO_PAN_ID;
    txframe->dstAddr = 0xFFFF;
    txframe->srcPan = PROTO_PAN_ID;
    radioTx(outBuffer);
    free(outBuffer);
}
uint8_t detectAP(const uint8_t channel) __reentrant {
    static uint16_t __xdata t;
    __xdata uint8_t *inBuffer = malloc(128);
    radioSetChannel(channel);
    radioRxFlush();
    for (uint8_t c = 1; c <= MAXIMUM_PING_ATTEMPTS; c++) {
        sendPing();
        t = timerGet() + (TIMER_TICKS_PER_MS * PING_REPLY_WINDOW);
        // while (timerGet() < t) {
        while ((uint16_t)(timerGet() - t) > 0x8000) {
            static int8_t __xdata ret;
            ret = radioRx(inBuffer);
            if (ret > 1) {
                if ((inBuffer[sizeof(struct MacFrameNormal) + 1] == channel) && (getPacketType(inBuffer) == PKT_PONG)) {
                    if (pktIsUnicast(inBuffer)) {
                        static __xdata struct MacFrameNormal *f;
                        f = (struct MacFrameNormal *)inBuffer;
                        memcpy(APmac, f->src, 8);
                        APsrcPan = f->pan;
                        free(inBuffer);
                        return c;
                    }
                }
            }
        }
    }
    free(inBuffer);
    return 0;
}

static void sendAvailDataReq(void) __reentrant {
    __xdata uint8_t *outBuffer = malloc(128);
    if (!outBuffer) pr("Failed to alloc outbuffer!!!\n");
    __xdata struct MacFrameBcast *txframe = (struct MacFrameBcast *)(outBuffer + 1);
    memset(outBuffer, 0, sizeof(struct MacFrameBcast) + sizeof(struct AvailDataReq) + 2 + 5);
    __xdata struct AvailDataReq *availreq = (struct AvailDataReq *)(outBuffer + 2 + sizeof(struct MacFrameBcast));
    outBuffer[0] = sizeof(struct MacFrameBcast) + sizeof(struct AvailDataReq) + 2 + 2;
    outBuffer[sizeof(struct MacFrameBcast) + 1] = PKT_AVAIL_DATA_REQ;
    memcpy(txframe->src, mSelfMac, 8);
    txframe->fcs.frameType = 1;
    txframe->fcs.ackReqd = 0;
    txframe->fcs.destAddrType = 2;
    txframe->fcs.srcAddrType = 3;
    txframe->seq = seq++;
    txframe->dstPan = PROTO_PAN_ID;
    txframe->dstAddr = 0xFFFF;
    txframe->srcPan = PROTO_PAN_ID;

    availreq->hwType = HW_TYPE;
    availreq->tagSoftwareVersion = FW_VERSION;
    availreq->currentChannel = currentChannel;
    availreq->batteryMv = batteryVoltage;
    availreq->capabilities = capabilities;
    availreq->lastPacketRSSI = lastRSSI;
    availreq->lastPacketLQI = lastLQI;
    availreq->wakeupReason = wakeUpReason;
    availreq->temperature = 20;
    availreq->customMode = tagSettings.customMode;

    availreq->reserved[0] = 0x55;
    availreq->reserved[7] = 0xAB;
    addCRC(availreq, sizeof(struct AvailDataReq));
    radioTx(outBuffer);
    free(outBuffer);
}
__xdata struct AvailDataInfo *getAvailDataInfo(void) __reentrant {
    // returns a pointer to a availdata struct
    radioRxFlush();
    __xdata uint8_t *inBuffer = malloc(130);
    if (!inBuffer) pr("Failed to alloc inbuffer\n");
    uint16_t t;
    for (uint8_t c = 0; c < DATA_REQ_MAX_ATTEMPTS; c++) {
        sendAvailDataReq();
        t = timerGet() + (TIMER_TICKS_PER_MS * DATA_REQ_RX_WINDOW_SIZE);
        // while (timerGet() < t) {
        while ((uint16_t)(timerGet() - t) > 0x8000) {
            int8_t ret = radioRx(inBuffer);
            if (ret > 1) {
                if (getPacketType(inBuffer) == PKT_AVAIL_DATA_INFO) {
                    if (checkCRC(inBuffer + sizeof(struct MacFrameNormal) + 1, sizeof(struct AvailDataInfo))) {
                        struct MacFrameNormal *f = (struct MacFrameNormal *)inBuffer;
                        memcpy(APmac, f->src, 8);
                        APsrcPan = f->pan;
                        dataReqLastAttempt = c;
#ifdef DEBUGPROTO
                        pr("PROTO: attempt %d okay\n", c);
#endif
                        __xdata struct AvailDataInfo *avail = malloc(sizeof(struct AvailDataInfo));
                        xMemCopyShort((void *)avail, (inBuffer + sizeof(struct MacFrameNormal) + 1), sizeof(struct AvailDataInfo));
                        free(inBuffer);
                        return avail;
                    }
                }
            }
        }
    }
    dataReqLastAttempt = DATA_REQ_MAX_ATTEMPTS;
    free(inBuffer);
    return NULL;
}

static bool processBlockPart(const __xdata struct blockPart *bp, __xdata uint8_t *blockbuffer) __reentrant {
    uint16_t start = bp->blockPart * BLOCK_PART_DATA_SIZE;
    uint16_t size = BLOCK_PART_DATA_SIZE;
    // validate if it's okay to copy data
    if (bp->blockId != curBlock.blockId) {
        return false;
    }
    if (start >= (BLOCK_XFER_BUFFER_SIZE - 1)) return false;
    if (bp->blockPart > BLOCK_MAX_PARTS) return false;
    if ((start + size) > BLOCK_XFER_BUFFER_SIZE) {
        size = BLOCK_XFER_BUFFER_SIZE - start;
    }

    if (checkCRC(bp, sizeof(struct blockPart) + BLOCK_PART_DATA_SIZE)) {
        //  copy block data to buffer
        xMemCopy((void *)(blockbuffer + start), (const void *)bp->data, size);
        // we don't need this block anymore, set bit to 0 so we don't request it again
        curBlock.requestedParts[bp->blockPart / 8] &= ~(1 << (bp->blockPart % 8));
        return true;
    } else {
        return false;
    }
}
static bool blockRxLoop(const uint16_t timeout, __xdata uint8_t *blockbuffer) __reentrant {
    uint16_t t;
    bool success = false;
    __xdata uint8_t *inBuffer = malloc(128);
    bool blockComplete = false;
    radioRxEnable(true);
    t = timerGet() + (TIMER_TICKS_PER_MS * (timeout + 20));
    while (((uint16_t)(timerGet() - t) > 0x8000) && (blockComplete != true)) {
        // while (timerGet() < t) {
        int8_t ret = radioRx(inBuffer);
        if (ret > 1) {
            if (getPacketType(inBuffer) == PKT_BLOCK_PART) {
                __xdata struct blockPart *bp = (struct blockPart *)(inBuffer + sizeof(struct MacFrameNormal) + 1);
                success = processBlockPart(bp, blockbuffer);
            } else {
                blockComplete = true;
                wdtPet();
                for (uint8_t c1 = 0; c1 < BLOCK_MAX_PARTS; c1++) {
                    if (curBlock.requestedParts[c1 / 8] & (1 << (c1 % 8))) {
                        blockComplete = false;
                        break;
                    }
                }
            }
        }
    }
    radioRxEnable(false);
    // radioRxFlush();
    free(inBuffer);
    return success;
}
static __xdata struct blockRequestAck *continueToRX(void) {
    __xdata struct blockRequestAck *blockAck = malloc(sizeof(struct blockRequestAck));
    blockAck->pleaseWaitMs = 0;
    return blockAck;
}
static void sendBlockRequest(bool requestPartialBlock) __reentrant {
    __xdata uint8_t *outBuffer = malloc(128);
    memset(outBuffer, 0, sizeof(struct MacFrameNormal) + sizeof(struct blockRequest) + 2 + 5);
    __xdata struct MacFrameNormal *f = (struct MacFrameNormal *)(outBuffer + 1);
    __xdata struct blockRequest *blockreq = (struct blockRequest *)(outBuffer + 2 + sizeof(struct MacFrameNormal));
    outBuffer[0] = sizeof(struct MacFrameNormal) + sizeof(struct blockRequest) + 2 + 2;
    if (requestPartialBlock) {
        ;
        outBuffer[sizeof(struct MacFrameNormal) + 1] = PKT_BLOCK_PARTIAL_REQUEST;
    } else {
        outBuffer[sizeof(struct MacFrameNormal) + 1] = PKT_BLOCK_REQUEST;
    }
    memcpy(f->src, mSelfMac, 8);
    memcpy(f->dst, APmac, 8);
    f->fcs.frameType = 1;
    f->fcs.secure = 0;
    f->fcs.framePending = 0;
    f->fcs.ackReqd = 0;
    f->fcs.panIdCompressed = 1;
    f->fcs.destAddrType = 3;
    f->fcs.frameVer = 0;  // 0
    f->fcs.srcAddrType = 3;
    f->seq = seq++;
    f->pan = APsrcPan;
    memcpy(blockreq, &curBlock, sizeof(struct blockRequest));
    addCRC(blockreq, sizeof(struct blockRequest));
    radioTx(outBuffer);
    free(outBuffer);
}
static __xdata struct blockRequestAck *performBlockRequest(bool requestPartialBlock) __reentrant {
    __xdata uint8_t *inBuffer = malloc(128);
    static uint16_t __xdata t;
    radioRxEnable(true);
    for (uint8_t c = 0; c < 10; c++) {  // was 30 attempts
        sendBlockRequest(requestPartialBlock);
        t = timerGet() + (TIMER_TICKS_PER_MS * (11UL + c / 5));
        do {
            static int8_t __xdata ret;
            ret = radioRx(inBuffer);
            if (ret > 1) {
                switch (getPacketType(inBuffer)) {
                    case PKT_BLOCK_REQUEST_ACK:
                        if (checkCRC((inBuffer + sizeof(struct MacFrameNormal) + 1), sizeof(struct blockRequestAck))) {
                            __xdata struct blockRequestAck *blockAck = malloc(sizeof(struct blockRequestAck));
                            xMemCopyShort(blockAck, (inBuffer + sizeof(struct MacFrameNormal) + 1), sizeof(struct blockRequestAck));
                            free(inBuffer);
                            return blockAck;
                        }
                        break;
                    case PKT_BLOCK_PART:
                        // block already started while we were waiting for a get block reply
                        // pr("!");
                        // processBlockPart((struct blockPart *)(inBuffer + sizeof(struct MacFrameNormal) + 1));
                        free(inBuffer);

                        return continueToRX();
                        break;
                    case PKT_CANCEL_XFER:
                        free(inBuffer);
                        return NULL;
                    default:
#ifdef DEBUGPROTO
                        pr("PROTO: pkt w/type %02X\n", getPacketType(inBuffer));
#endif
                        break;
                }
            }
        } while ((uint16_t)(timerGet() - t) > 0x8000);

        //} while (timerGet() < t);
    }
    free(inBuffer);
    return continueToRX();
}
static void sendXferCompletePacket(void) {
    __xdata uint8_t *outBuffer = malloc(32);
    memset(outBuffer, 0, sizeof(struct MacFrameNormal) + 2 + 4);
    __xdata struct MacFrameNormal *f = (struct MacFrameNormal *)(outBuffer + 1);
    outBuffer[0] = sizeof(struct MacFrameNormal) + 2 + 2;
    outBuffer[sizeof(struct MacFrameNormal) + 1] = PKT_XFER_COMPLETE;
    memcpy(f->src, mSelfMac, 8);
    memcpy(f->dst, APmac, 8);
    f->fcs.frameType = 1;
    f->fcs.secure = 0;
    f->fcs.framePending = 0;
    f->fcs.ackReqd = 0;
    f->fcs.panIdCompressed = 1;
    f->fcs.destAddrType = 3;
    f->fcs.frameVer = 0;
    f->fcs.srcAddrType = 3;
    f->pan = APsrcPan;
    f->seq = seq++;
    radioTx(outBuffer);
    free(outBuffer);
}
static void sendXferComplete(void) __reentrant {
    radioRxEnable(true);
    __xdata uint8_t *inBuffer = malloc(128);
    for (uint8_t c = 0; c < 16; c++) {
        sendXferCompletePacket();
        uint32_t start = timerGet();
        while ((timerGet() - start) < (TIMER_TICKS_PER_MS * 10UL)) {
            int8_t ret = radioRx(inBuffer);
            if (ret > 1) {
                if (getPacketType(inBuffer) == PKT_XFER_COMPLETE_ACK) {
#ifdef DEBUGPROTO
                    pr("PROTO: XFC ACK\n");
#endif
                    free(inBuffer);
                    return;
                }
            }
        }
    }
#ifdef DEBUGPROTO
    pr("PROTO: XFC NACK!\n");
#endif
    free(inBuffer);
    return;
}
static bool validateBlockData(__xdata uint8_t *blockbuffer) {
    __xdata struct blockData *bd = (__xdata struct blockData *)blockbuffer;
    // pr("expected len = %04X, checksum=%04X\n", bd->size, bd->checksum);
    uint16_t t = 0;
    for (uint16_t c = 0; c < bd->size; c++) {
        t += bd->data[c];
    }
    // pr("calculated = %04X\n", t);
    return bd->checksum == t;
}
__xdata uint8_t *getDataBlock(const uint16_t blockSize) __reentrant {
    uint8_t partsThisBlock = 0;
    uint8_t blockAttempts = 0;
    blockAttempts = BLOCK_TRANSFER_ATTEMPTS;
    if (blockSize == BLOCK_DATA_SIZE) {
        partsThisBlock = BLOCK_MAX_PARTS;
        memset(curBlock.requestedParts, 0xFF, BLOCK_REQ_PARTS_BYTES);
    } else {
        partsThisBlock = (sizeof(struct blockData) + blockSize) / BLOCK_PART_DATA_SIZE;
        if ((sizeof(struct blockData) + blockSize) % BLOCK_PART_DATA_SIZE) partsThisBlock++;
        memset(curBlock.requestedParts, 0x00, BLOCK_REQ_PARTS_BYTES);
        for (uint8_t c = 0; c < partsThisBlock; c++) {
            curBlock.requestedParts[c / 8] |= (1 << (c % 8));
        }
    }

    bool requestPartialBlock = false;  // this forces the AP to request the block data from the host

    __xdata uint8_t *blockbuffer = NULL;

    while (blockAttempts--) {
        wdtPet();
#ifndef DEBUGBLOCKS
        pr("REQ %d ", curBlock.blockId);
#else
        pr("REQ %d[", curBlock.blockId);
        for (uint8_t c = 0; c < BLOCK_MAX_PARTS; c++) {
            if ((c != 0) && (c % 8 == 0)) pr("][");
            if (curBlock.requestedParts[c / 8] & (1 << (c % 8))) {
                pr("R");
            } else {
                pr("_");
            }
        }
        pr("]\n");
#endif
        powerUp(INIT_RADIO);
        __xdata struct blockRequestAck *ack = performBlockRequest(requestPartialBlock);

        if (ack == NULL) {
#ifdef DEBUGPROTO
            pr("PROTO: Cancelled request\n");
#endif
            if (blockbuffer) free(blockbuffer);
            return false;
        }

        if (!blockbuffer) {
            blockbuffer = malloc(BLOCK_XFER_BUFFER_SIZE + 16);
            if (!blockbuffer) {
                pr("failed to malloc blockbuffer. This sucks.\n");
                if (ack) free(ack);
                return NULL;
            }
        }

        if (ack->pleaseWaitMs) {  // SLEEP - until the AP is ready with the data
                                  // if (ack->pleaseWaitMs < 35) {
            // timerDelay(ack->pleaseWaitMs);
            // } else {
            powerDown(INIT_RADIO);
            doSleep(ack->pleaseWaitMs + 40);
            wdtPet();
            powerUp(INIT_RADIO);
            radioRxEnable(true);
            //}
        } else {
            // immediately start with the reception of the block data
        }
        blockRxLoop(260, blockbuffer);  // BLOCK RX LOOP - receive a block, until the timeout has passed
        powerDown(INIT_RADIO);

#ifdef DEBUGBLOCKS
        pr("RX  %d[", curBlock.blockId);
        for (uint8_t c = 0; c < BLOCK_MAX_PARTS; c++) {
            if ((c != 0) && (c % 8 == 0)) pr("][");
            if (curBlock.requestedParts[c / 8] & (1 << (c % 8))) {
                pr(".");
            } else {
                pr("R");
            }
        }
        pr("]\n");
#endif
        // check if we got all the parts we needed, e.g: has the block been completed?
        bool blockComplete = true;
        for (uint8_t c1 = 0; c1 < partsThisBlock; c1++) {
            if (curBlock.requestedParts[c1 / 8] & (1 << (c1 % 8))) blockComplete = false;
        }

        if (blockComplete) {
#ifndef DEBUGBLOCKS
            pr("- COMPLETE\n");
#endif
            if (validateBlockData(blockbuffer)) {
                // block download complete, validated
                if (ack) free(ack);
                return blockbuffer;
            } else {
                for (uint8_t c = 0; c < partsThisBlock; c++) {
                    curBlock.requestedParts[c / 8] |= (1 << (c % 8));
                }
                requestPartialBlock = false;
#ifdef DEBUGPROTO
                pr("PROTO: blk failed validation!\n");
#endif
            }
        } else {
#ifndef DEBUGBLOCKS
            pr("- INCOMPLETE\n");
#endif
            // block incomplete, re-request a partial block
            requestPartialBlock = true;
        }
        if (ack) free(ack);
    }
#ifdef DEBUGPROTO
    pr("PROTO: failed getting block\n");
#endif
    free(blockbuffer);
    return NULL;
}

// EEprom related stuff
static uint32_t getAddressForSlot(const uint8_t s) {
    return EEPROM_IMG_START + (EEPROM_IMG_EACH * s);
}
static void getNumSlots(void) {
    eeSize = eepromGetSize();
    uint16_t nSlots = mathPrvDiv32x16(eeSize - EEPROM_IMG_START, EEPROM_IMG_EACH >> 8) >> 8;
    if (eeSize < EEPROM_IMG_START || !nSlots) {
#ifdef DEBUGEEPROM
        pr("EEPROM: eeprom is too small\n");
#endif
        while (1);
    } else if (nSlots >> 8) {
#ifdef DEBUGEEPROM
        pr("EEPROM: eeprom is too big, some will be unused\n");
#endif
        imgSlots = 254;
    } else
        imgSlots = nSlots;
#ifdef DEBUGPROTO
    pr("PROTO: %d image slots total\n", imgSlots);
#endif
}
static uint8_t findSlotVer(const __xdata uint8_t *ver) __reentrant {
#ifdef DEBUGBLOCKS
    return 0xFF;
#endif
    // return 0xFF;  // remove me! This forces the tag to re-download each and every upload without checking if it's already in the eeprom somewhere
    __xdata struct EepromImageHeader *eih = malloc(sizeof(struct EepromImageHeader));  //(struct EepromImageHeader __xdata *)blockbuffer;
    for (uint8_t c = 0; c < imgSlots; c++) {
        eepromRead(getAddressForSlot(c), eih, sizeof(struct EepromImageHeader));
        if (xMemEqual4(&eih->validMarker, &markerValid)) {
            if (xMemEqual(&eih->version, (void *)ver, 8)) {
                free(eih);
                return c;
            }
        }
    }
    free(eih);
    return 0xFF;
}

static uint8_t findNextSlot(const __xdata struct AvailDataInfo *avail) {
    // new transfer
    powerUp(INIT_EEPROM);

    // go to the next image slot
    uint8_t startingSlot = nextImgSlot;
#pragma disable_warning 110
    // if we encounter a special image type, start looking from slot 0, to prevent the image being overwritten when we do an OTA update
    if (avail->dataTypeArgument & 0xFC != 0x00) {
        startingSlot = 0;
    }
#pragma disable_warning 110
    while (1) {
        nextImgSlot++;
        if (nextImgSlot >= imgSlots) nextImgSlot = 0;

        __xdata struct EepromImageHeader *eih = malloc(sizeof(struct EepromImageHeader));
        eepromRead(getAddressForSlot(nextImgSlot), eih, sizeof(struct EepromImageHeader));
        // check if the marker is indeed valid
        if (xMemEqual4(&eih->validMarker, &markerValid)) {
            struct imageDataTypeArgStruct *eepromDataArgument = (struct imageDataTypeArgStruct *)&(eih->argument);
            // normal type, we can overwrite this
            if (eepromDataArgument->specialType == 0x00) {
                free(eih);
                break;
            }
            // can't overwrite, but we should free the eeprom header anyway
            free(eih);
        } else {
            // bullshit marker, so safe to overwrite
            free(eih);
            break;
        }
    }

    xferImgSlot = nextImgSlot;

    uint8_t attempt = 5;
    while (attempt--) {
        if (eepromErase(getAddressForSlot(xferImgSlot), EEPROM_IMG_EACH / EEPROM_ERZ_SECTOR_SZ)) goto eraseSuccess;
    }
eepromFail:
    powerDown(INIT_RADIO);
    pr("PROTO: EEPROM fail?\n");
    powerDown(INIT_EEPROM);
    doSleep(-1);
    // wdtDeviceReset();
eraseSuccess:
    powerDown(INIT_EEPROM);

    return nextImgSlot;
}

static uint8_t decompressImageG5(const __xdata struct AvailDataInfo *avail, uint8_t compressedImgSlot) __reentrant {
    // find next slot to decompress image into
    uint8_t decompressedSlot = findNextSlot(avail);
#ifdef DEBUGG5DEC
    pr("G5: reading data from slot %d\n", compressedImgSlot);
#endif
    powerUp(INIT_EEPROM);

#define READBUFFERSIZE 1024UL
#define MIN_REMAINING_READBUFFER 256UL
    uint32_t readCurOffset = getAddressForSlot(compressedImgSlot) + sizeof(struct EepromImageHeader);

    __xdata uint8_t *readbuffer = malloc(READBUFFERSIZE);
    __xdata uint8_t *writebuffer = malloc((SCREEN_WIDTH / 8) + 1);  // hmm.

    // start reading the first block
    eepromRead(readCurOffset, readbuffer, READBUFFERSIZE);

    uint8_t imageHeaderSize = readbuffer[0];
    uint8_t imageBpp = readbuffer[5];
    if (imageBpp > 0x20) imageBpp -= 0x20;

    uint16_t max_y = SCREEN_HEIGHT;
    if (imageBpp == 2) max_y *= 2;  // we use double the height for the second color layer

    __xdata G5DECIMAGE *g5dec = (__xdata G5DECIMAGE *)malloc(sizeof(G5DECIMAGE));  // max about 2600 bytes
    if (!g5dec) {
#ifdef DEBUGG5DEC
        pr("G5: Failed to allocate g5 struct\n");
#endif
    }

    int rc = g5_decode_init(g5dec, SCREEN_WIDTH, max_y, readbuffer + imageHeaderSize, READBUFFERSIZE);  //(int)avail->dataSize);
    if (rc != G5_SUCCESS) {
#ifdef DEBUGG5DEC
        pr("G5: Failed to init: Error %d\n", rc);
#endif
    }

    for (uint16_t y = 0; y < max_y; y++) {
        rc = g5_decode_line(g5dec, writebuffer);
        wdtPet();
        // check for highwater, load a new block if we're near the end of the buffer
        if (((uint32_t)g5dec->pBuf) > ((uint32_t)readbuffer + (READBUFFERSIZE - MIN_REMAINING_READBUFFER))) {
#ifdef DEBUGG5DEC
            pr("G5: Loading new compressed block\n");
#endif
            uint16_t curBlockReadOffset = g5dec->pBuf - readbuffer;
            readCurOffset += curBlockReadOffset;
            g5dec->pBuf = readbuffer;
            // load new block

            eepromRead(readCurOffset, readbuffer, READBUFFERSIZE);
        }
        eepromWrite(getAddressForSlot(decompressedSlot) + sizeof(struct EepromImageHeader) + ((SCREEN_WIDTH / 8) * y), writebuffer, SCREEN_WIDTH / 8);
        if ((rc != G5_SUCCESS) && (rc != G5_DECODE_COMPLETE)) {
#ifdef DEBUGG5DEC
            pr("G5: Error at line y=%d\n", y);
#endif
            break;
        }
    }
#ifdef DEBUGG5DEC
    pr("G5: Last block status code %d\n", rc);
#endif
    free(readbuffer);
    free(writebuffer);
    free(g5dec);

    // mark slot with compressed dataver, corrected datatype
    __xdata struct EepromImageHeader *eih = (__xdata struct EepromImageHeader *)malloc(sizeof(struct EepromImageHeader));
    xMemCopy8(&eih->version, &avail->dataVer);
    eih->validMarker = EEPROM_IMG_VALID;
    eih->id = ++curHighSlotId;
    eih->size = avail->dataSize;  // THIS SHOULD BE UPDATED TO THE DECOMPRESSED SIZE! (but we don't really use this stuff anyway)
    if (imageBpp == 1) {
        eih->dataType = DATATYPE_IMG_RAW_1BPP;
    } else {
        eih->dataType = DATATYPE_IMG_RAW_2BPP;
    }
    eih->argument = avail->dataTypeArgument;
    eepromWrite(getAddressForSlot(decompressedSlot), eih, sizeof(struct EepromImageHeader));
    free(eih);

    // invalidate slot with compressed data
    __xdata uint8_t *temp = malloc(16);
    memset(temp, 0x00, 16);
    eepromWrite(getAddressForSlot(compressedImgSlot), temp, 16);
    free(temp);

    return decompressedSlot;
}

// making this reentrant saves one byte of idata...
uint8_t __xdata findSlotDataTypeArg(uint8_t arg) __reentrant {
    arg &= (0xF8);  // unmatch with the 'preload' bit and LUT bits
    __xdata struct EepromImageHeader *eih = malloc(sizeof(struct EepromImageHeader));
    for (uint8_t c = 0; c < imgSlots; c++) {
        eepromRead(getAddressForSlot(c), eih, sizeof(struct EepromImageHeader));
        if (xMemEqual4(&eih->validMarker, &markerValid)) {
            if ((eih->argument & 0xF8) == arg) {
                free(eih);
                return c;
            }
        }
    }
    free(eih);
    return 0xFF;
}
uint8_t getEepromImageDataArgument(const uint8_t slot) {
    __xdata struct EepromImageHeader *eih = malloc(sizeof(struct EepromImageHeader));
    eepromRead(getAddressForSlot(slot), eih, sizeof(struct EepromImageHeader));
    uint8_t __xdata ret = eih->argument;
    free(eih);
    return ret;
}
uint8_t __xdata findNextSlideshowImage(uint8_t start) __reentrant {
    __xdata struct EepromImageHeader *eih = malloc(sizeof(struct EepromImageHeader));
    uint8_t c = start;
    while (1) {
        c++;
        if (c > imgSlots) c = 0;
        if (c == start) return c;
        eepromRead(getAddressForSlot(c), eih, sizeof(struct EepromImageHeader));
        if (xMemEqual4(&eih->validMarker, &markerValid)) {
            if ((eih->argument & 0xF8) == (CUSTOM_IMAGE_SLIDESHOW << 3)) {
                free(eih);
                return c;
            }
        }
    }
}

static void eraseUpdateBlock(void) {
#if defined(DEBUGOTA)
    pr("OTA: Doing erase, starting from %lu\n", eeSize - OTA_UPDATE_SIZE);
#endif
    eepromErase(eeSize - OTA_UPDATE_SIZE, OTA_UPDATE_SIZE / EEPROM_ERZ_SECTOR_SZ);
}
static void eraseImageBlock(const uint8_t c) {
    eepromErase(getAddressForSlot(c), EEPROM_IMG_EACH / EEPROM_ERZ_SECTOR_SZ);
}
static void saveUpdateBlockData(uint8_t blockId, __xdata uint8_t *blockbuffer) __reentrant {
    eepromWrite(eeSize - OTA_UPDATE_SIZE + (blockId * BLOCK_DATA_SIZE), blockbuffer + sizeof(struct blockData), BLOCK_DATA_SIZE);
}
static void saveImgBlockData(const uint8_t imgSlot, const uint8_t blockId, __xdata uint8_t *blockbuffer) __reentrant {
    uint16_t length = EEPROM_IMG_EACH - (sizeof(struct EepromImageHeader) + (blockId * BLOCK_DATA_SIZE));
    if (length > 4096) length = 4096;

    eepromWrite(getAddressForSlot(imgSlot) + sizeof(struct EepromImageHeader) + (blockId * BLOCK_DATA_SIZE), blockbuffer + sizeof(struct blockData), length);
}
void eraseImageBlocks(void) {
    for (uint8_t c = 0; c < imgSlots; c++) {
        eraseImageBlock(c);
    }
}

void drawImageFromEeprom(const uint8_t imgSlot) {
    drawImageAtAddress(getAddressForSlot(imgSlot));
}

static uint32_t getHighSlotId(void) __reentrant {
    uint32_t temp = 0;
    __xdata struct EepromImageHeader *eih = malloc(sizeof(struct EepromImageHeader));
    for (uint8_t c = 0; c < imgSlots; c++) {
        eepromRead(getAddressForSlot(c), eih, sizeof(struct EepromImageHeader));
        if (xMemEqual4(&eih->validMarker, &markerValid)) {
            if (temp < eih->id) {
                temp = eih->id;
                nextImgSlot = c;
            }
        }
    }
#ifdef DEBUGPROTO
    pr("PROTO: found high id=%lu in slot %d\n", temp, nextImgSlot);
#endif
    free(eih);
    return temp;
}

static bool downloadFWUpdate(const __xdata struct AvailDataInfo *avail) __reentrant {
    static uint16_t __xdata dataRequestSize = 0;
    static uint16_t __xdata otaSize = 0;
    // check if we already started the transfer of this information & haven't completed it
    if (xMemEqual((const __xdata void *)&avail->dataVer, (const __xdata void *)&xferDataInfo.dataVer, 8) && xferDataInfo.dataSize) {
        // looks like we did. We'll carry on where we left off.
    } else {
#if defined(DEBUGOTA)
        pr("OTA: Start update!\n");
#endif
        // start, or restart the transfer from 0. Copy data from the AvailDataInfo struct, and the struct intself. This forces a new transfer
        curBlock.blockId = 0;
        xMemCopy8(&(curBlock.ver), &(avail->dataVer));
        curBlock.type = avail->dataType;
        xMemCopyShort(&xferDataInfo, (void *)avail, sizeof(struct AvailDataInfo));
        eraseUpdateBlock();
        otaSize = xferDataInfo.dataSize;
    }

    while (xferDataInfo.dataSize) {
        if (xferDataInfo.dataSize > BLOCK_DATA_SIZE) {
            // more than one block remaining
            dataRequestSize = BLOCK_DATA_SIZE;
        } else {
            // only one block remains
            dataRequestSize = xferDataInfo.dataSize;
        }
        __xdata uint8_t *blockbuffer;
        if (blockbuffer = getDataBlock(dataRequestSize)) {
            // succesfully downloaded datablock, save to eeprom
            powerUp(INIT_EEPROM);
            saveUpdateBlockData(curBlock.blockId, blockbuffer);
            powerDown(INIT_EEPROM);
            curBlock.blockId++;
            xferDataInfo.dataSize -= dataRequestSize;
            free(blockbuffer);
        } else {
            // failed to get the block we wanted, we'll stop for now, maybe resume later
            return false;
        }
    }
    powerUp(INIT_EEPROM);
    if (!validateMD5(eeSize - OTA_UPDATE_SIZE, otaSize)) {
#if defined(DEBUGOTA)
        pr("OTA: MD5 verification failed!\n");
#endif
        // if not valid, restart transfer from the beginning
        curBlock.blockId = 0;
        powerDown(INIT_EEPROM);
        return false;
    }
#if defined(DEBUGOTA)
    pr("OTA: MD5 pass!\n");
#endif

    // no more data, download complete
    return true;
}

static bool downloadImageDataToEEPROM(const __xdata struct AvailDataInfo *avail) __reentrant {
    uint16_t imageSize = 0;
    //  check if we already started the transfer of this information & haven't completed it
    if (xMemEqual((const __xdata void *)&avail->dataVer, (const __xdata void *)&xferDataInfo.dataVer, 8) &&
        (xferDataInfo.dataTypeArgument == avail->dataTypeArgument) &&
        xferDataInfo.dataSize) {
// looks like we did. We'll carry on where we left off.
#ifdef DEBUGPROTO
        pr("PROTO: restarting image download\n");
#endif
    } else {
        // new transfer
        xferImgSlot = findNextSlot(avail);
#ifdef DEBUGPROTO
        pr("PROTO: new download, writing to slot %d\n", xferImgSlot);
#endif
        // start, or restart the transfer. Copy data from the AvailDataInfo struct, and the struct intself. This forces a new transfer
        curBlock.blockId = 0;
        xMemCopy8(&(curBlock.ver), &(avail->dataVer));
        curBlock.type = avail->dataType;
        xMemCopyShort(&xferDataInfo, (void *)avail, sizeof(struct AvailDataInfo));
        imageSize = xferDataInfo.dataSize;
    }

    while (xferDataInfo.dataSize) {
        uint16_t dataRequestSize;
        if (xferDataInfo.dataSize > BLOCK_DATA_SIZE) {
            // more than one block remaining
            dataRequestSize = BLOCK_DATA_SIZE;
        } else {
            // only one block remains
            dataRequestSize = xferDataInfo.dataSize;
        }
        __xdata uint8_t *blockbuffer = NULL;
        if (blockbuffer = getDataBlock(dataRequestSize)) {
            // succesfully downloaded datablock, save to eeprom
            powerUp(INIT_EEPROM);
#ifdef DEBUGBLOCKS
            pr("BLOCKS: Saving block %d to slot %d\n", curBlock.blockId, xferImgSlot);
#endif
            saveImgBlockData(xferImgSlot, curBlock.blockId, blockbuffer);
            powerDown(INIT_EEPROM);
            curBlock.blockId++;
            xferDataInfo.dataSize -= dataRequestSize;
            free(blockbuffer);
        } else {
            // failed to get the block we wanted, we'll stop for now, probably resume later
            return false;
        }
    }
    // no more data, download complete

    // validate MD5
    powerUp(INIT_EEPROM);
    // #ifdef VALIDATE_IMAGE_MD5
    if (!validateMD5(getAddressForSlot(xferImgSlot) + sizeof(struct EepromImageHeader), imageSize)) {
        // if not valid, restart transfer from the beginning
        curBlock.blockId = 0;
        powerDown(INIT_EEPROM);
        return false;
    }
    wdtPet();
    // #endif
    //  borrow the blockbuffer temporarily
    __xdata struct EepromImageHeader *eih = malloc(sizeof(struct EepromImageHeader));
    xMemCopy8(&eih->version, &xferDataInfo.dataVer);
    eih->validMarker = EEPROM_IMG_VALID;
    eih->id = ++curHighSlotId;
    eih->size = imageSize;
    eih->dataType = xferDataInfo.dataType;
    eih->argument = xferDataInfo.dataTypeArgument;

#ifdef DEBUGBLOCKS
    pr("BLOCKS: Now writing datatype 0x%02X to slot %d\n", xferDataInfo.dataType, xferImgSlot);
#endif
    eepromWrite(getAddressForSlot(xferImgSlot), eih, sizeof(struct EepromImageHeader));
    free(eih);
    powerDown(INIT_EEPROM);

    // check if we need to decompress a G5-compressed image
    if (xferDataInfo.dataType == DATATYPE_IMG_G5) {
        xferImgSlot = decompressImageG5(&xferDataInfo, xferImgSlot);
    }

    return true;
}

inline bool processImageDataAvail(__xdata struct AvailDataInfo *avail) __reentrant {
    struct imageDataTypeArgStruct arg = {0};  // this is related to the function below, but if declared -inside- the function, it gets cleared during sleep...
    *((uint8_t *)arg) = avail->dataTypeArgument;
    if (arg.preloadImage) {
#ifdef DEBUGPROTO
        pr("PROTO: Preloading image with type 0x%02X from arg 0x%02X\n", arg.specialType, avail->dataTypeArgument);
#endif
        powerUp(INIT_EEPROM);
        switch (arg.specialType) {
            // check if a slot with this argument is already set; if so, erase. Only one of each arg type should exist
            default: {
                uint8_t slot = findSlotDataTypeArg(avail->dataTypeArgument);
                if (slot != 0xFF) {
                    eepromErase(getAddressForSlot(slot), EEPROM_IMG_EACH / EEPROM_ERZ_SECTOR_SZ);
                }
            } break;
            // regular image preload, there can be multiple of this type in the EEPROM
            case CUSTOM_IMAGE_NOCUSTOM: {
                // check if a version of this already exists
                uint8_t slot = findSlotVer(&(avail->dataVer));
                if (slot != 0xFF) {
                    powerUp(INIT_RADIO);
                    sendXferComplete();
                    powerDown(INIT_RADIO);
                    return true;
                }
            } break;
            case CUSTOM_IMAGE_SLIDESHOW:
                break;
        }
        powerDown(INIT_EEPROM);
#ifdef DEBUGPROTO
        pr("PROTO: downloading preload image...\n");
#endif
        if (downloadImageDataToEEPROM(avail)) {
            // sets xferImgSlot to the right slot
#ifdef DEBUGPROTO
            pr("PROTO: preload complete!\n");
#endif
            powerUp(INIT_RADIO);
            sendXferComplete();
            powerDown(INIT_RADIO);
            return true;
        } else {
            return false;
        }

    } else {
        // check if we're currently displaying this data payload
        if (xMemEqual((const __xdata void *)&avail->dataVer, (const __xdata void *)curDispDataVer, 8)) {
            // currently displayed, not doing anything except for sending an XFC
#ifdef DEBUGPROTO
            pr("PROTO: currently shown image, send xfc\n");
#endif
            powerUp(INIT_RADIO);
            sendXferComplete();
            powerDown(INIT_RADIO);
            return true;

        } else {
            // currently not displayed
#ifdef DEBUGPROTO
            pr("PROTO: currently not shown image\n");
#endif
            // try to find the data in the SPI EEPROM
            powerUp(INIT_EEPROM);
            uint8_t findImgSlot = findSlotVer(&(avail->dataVer));
            powerDown(INIT_EEPROM);

            // Is this image already in a slot somewhere
            if (findImgSlot != 0xFF) {
#ifdef DEBUGPROTO
                pr("PROTO: Found image in EEPROM\n");
#endif
                // found a (complete)valid image slot for this version
                powerUp(INIT_RADIO);
                sendXferComplete();
                powerDown(INIT_RADIO);

                // mark as completed and draw from EEPROM
                xMemCopyShort(&xferDataInfo, (void *)avail, sizeof(struct AvailDataInfo));
                xferDataInfo.dataSize = 0;  // mark as transfer not pending
                curImgSlot = findImgSlot;
                powerUp(INIT_EEPROM);
                drawImageFromEeprom(findImgSlot);
                powerDown(INIT_EEPROM);
            } else {
// not found in cache, prepare to download
#ifdef DEBUGPROTO
                pr("PROTO: downloading image...\n");
#endif
                if (downloadImageDataToEEPROM(avail)) {
// sets xferImgSlot to the right slot
#ifdef DEBUGPROTO
                    pr("PROTO: download complete!\n");
#endif
                    powerUp(INIT_RADIO);
                    sendXferComplete();
                    powerDown(INIT_RADIO);

                    // not preload, draw now
                    curImgSlot = xferImgSlot;
                    powerUp(INIT_EEPROM);
                    drawImageFromEeprom(xferImgSlot);
                    powerDown(INIT_EEPROM);
                } else {
                    return false;
                }
            }
            //  keep track on what is currently displayed
            xMemCopy8(curDispDataVer, &xferDataInfo.dataVer);
            return true;
        }
    }
}

bool processAvailDataInfo(__xdata struct AvailDataInfo *avail) __reentrant {
    if (((avail->dataType == DATATYPE_FW_UPDATE) && (avail->dataSize > 65536)) || ((avail->dataType != DATATYPE_FW_UPDATE) && (avail->dataSize > EEPROM_IMG_EACH))) {
        powerUp(INIT_RADIO);
        sendXferComplete();
        powerDown(INIT_RADIO);
#ifdef DEBUGPROTO
        pr("PROTO: availData too large\n");
#endif
        return true;
    }
    switch (avail->dataType) {
        case DATATYPE_IMG_BMP:
        case DATATYPE_IMG_DIFF:
        case DATATYPE_IMG_RAW_1BPP:
        case DATATYPE_IMG_RAW_2BPP:
        case DATATYPE_IMG_G5:
            return processImageDataAvail(avail);
            break;
        case DATATYPE_FW_UPDATE:
            powerUp(INIT_EEPROM);
            if (downloadFWUpdate(avail)) {
#if defined(DEBUGOTA)
                pr("OTA: Download complete\n");
#endif
                powerUp(INIT_RADIO);
                sendXferComplete();
                powerDown(INIT_RADIO);

                powerUp(INIT_EEPROM);
                if (validateFWMagic()) {
#if defined(DEBUGOTA)
                    pr("OTA: Valid magic number\n");
                    pr("OTA: We'll start flashing from %lu\n", eeSize - OTA_UPDATE_SIZE);
#endif
                    programFlashFromEEPROM(eeSize - OTA_UPDATE_SIZE, 64);
                    //  ends in WDT reset
                } else {
#if defined(DEBUGOTA)
                    pr("OTA: Invalid magic number!\n");
#endif
                    fastNextCheckin = true;
                    powerDown(INIT_EEPROM);
                    wakeUpReason = WAKEUP_REASON_FAILED_OTA_FW;
                    memset(curDispDataVer, 0x00, 8);
                }

            } else {
                return false;
            }
            break;
        case DATATYPE_TAG_CONFIG_DATA:
            if (xferDataInfo.dataSize == 0 && xMemEqual((const __xdata void *)&avail->dataVer, (const __xdata void *)&xferDataInfo.dataVer, 8)) {
#ifdef DEBUGPROTO
                pr("PROTO: this was the same as the last transfer, disregard\n");
#endif
                powerUp(INIT_RADIO);
                sendXferComplete();
                powerDown(INIT_RADIO);
                return true;
            }
            curBlock.blockId = 0;
            xMemCopy8(&(curBlock.ver), &(avail->dataVer));
            curBlock.type = avail->dataType;
            xMemCopyShort(&xferDataInfo, (void *)avail, sizeof(struct AvailDataInfo));
            __xdata uint8_t *blockbuffer = NULL;
            if (blockbuffer = getDataBlock(avail->dataSize)) {
                xferDataInfo.dataSize = 0;  // mark as transfer not pending
                powerUp(INIT_EEPROM);
                loadSettingsFromBuffer(sizeof(struct blockData) + blockbuffer);
                powerDown(INIT_EEPROM);
                powerUp(INIT_RADIO);
                sendXferComplete();
                powerDown(INIT_RADIO);
                free(blockbuffer);
                return true;
            }
            return false;
            break;
        case DATATYPE_COMMAND_DATA:
#ifdef DEBUGPROTO
            pr("PROTO: CMD received\n");
#endif
            powerUp(INIT_RADIO);
            sendXferComplete();
            powerDown(INIT_RADIO);
            executeCommand(avail->dataTypeArgument);
            return true;
            break;
    }
#ifdef DEBUGPROTO
    pr("PROTO: Got something I didn't understand _at all_ type = %02X\n", avail->dataType);
#endif
    return false;
}

bool validateMD5(uint32_t addr, uint16_t len) __reentrant {
    powerUp(INIT_EEPROM);
    __xdata uint8_t *blockbuffer = malloc(256);
    md5Init();
    while (len) {
        eepromRead(addr, blockbuffer, 256);
        if (len >= 256) {
            md5Update(blockbuffer, 256);
            len -= 256;
            addr += 256;
        } else {
            md5Update(blockbuffer, len);
            len = 0;
        }
        wdtPet();
    }
    md5Finalize();
    if (xMemEqual((const __xdata void *)ctxdigest, (const __xdata void *)&xferDataInfo.dataVer, 8)) {
#ifdef DEBUGPROTO
        pr("PROTO: MD5 pass!\n");
#endif
        free(blockbuffer);
        return true;
    } else {
#ifdef DEBUGPROTO
        pr("PROTO: MD5 fail...\n");
#endif
        free(blockbuffer);
        return false;
    }
}

bool validateFWMagic(void) {
    MEMCTR = 0;  // map flash page 1 into 0x8000
    __xdata uint8_t *blockbuffer = malloc(256);
    eepromRead(eeSize - OTA_UPDATE_SIZE, blockbuffer, 256);
    if (xMemEqual((const __xdata void *)((uintptr_t)0x808b),
                  (const __xdata void *)(blockbuffer + 0x8b), 8)) {
#ifdef DEBUGOTA
        pr("OTA: magic number matches! good fw\n");
#endif
        free(blockbuffer);
        return true;
    } else {
#ifdef DEBUGOTA
        pr("OTA: this probably isn't a (recent) firmware file\n");
#endif
        free(blockbuffer);
        return false;
    }
}

void initializeProto(void) {
    getNumSlots();
    curHighSlotId = getHighSlotId();
    // eeSize = eepromGetSize(); // happens in getNumSlots already
}
