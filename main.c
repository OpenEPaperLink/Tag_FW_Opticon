// Credits to:
// Jelmer Bruijn
// Dmitry Grinberg
// Stephen Erisman
// ChatGPT

#include "hal.h"
#include "softuart.h"
#include "printf.h"
#include "dma.h"
#include "delay.h"
#include "eeprom.h"

#include <stdlib.h>

#include "ota.h"

#include "drawing.h"
#include "bitmaps.h"
#include "string.h"
#include "meinradio.h"
#include "proto.h"

#include "powermgt.h"
#include "settings.h"

#include "screen.h"
#include "tagprofile.h"

#include "userinterface.h"

#include "wdt.h"

#define __packed
#include "../shared/oepl-definitions.h"
#include "../shared/oepl-proto.h"

#define TAG_MODE_CHANSEARCH 0
#define TAG_MODE_ASSOCIATED 1

#define MAC_ADDRESS_LOCATION 0x780C

uint8_t currentTagMode = TAG_MODE_CHANSEARCH;
static bool __xdata secondLongCheckIn = false;  // send another full request if the previous was a special reason

#define FWMAGICOFFSET 0x008b
static const uint64_t __code __at(FWMAGICOFFSET) firmwaremagic = (0xdeadd0d0beefcafeull) + HW_TYPE;

// This firmware requires a rather large heap. This overrides SDCC's built-in heap size (1024)
__xdata char __sdcc_heap[HEAP_SIZE];
const unsigned int __sdcc_heap_size = HEAP_SIZE;

void memtest(void) __reentrant {
    uint8_t maxblock = 0;
    uint8_t *block;
    // pr("Testing memory\n");
    for (uint8_t c = 120; c < 200; c++) {
        block = malloc(c * 32);
        if (!block) {
            break;
        } else {
            free(block);
            maxblock = c;
            pr("\r%d bytes", (uint16_t)maxblock * 32);  // r
        }
        if (c > 170) {
            pr("\nWe shouldn't be able to allocate so much memory.\nSomething died, system halted.\n");
            while (1) {
            }
        }
    }
    block = malloc(maxblock * 32);
    if (!block) {
        pr("\n\n*** Failed to allocate block after finding maxblock\n");
    } else {
        for (uint16_t c = 0; c < (maxblock * 32); c++) {
            block[c] = c % 256;
        }
        for (uint16_t c = 0; c < (maxblock * 32); c++) {
            if (block[c] != (uint8_t)(c % 256)) {
                pr("\n*** Error @ %d - Expected %02X, read %02X", c, (uint8_t)(c % 256), block[c]);
                break;
            }
        }
        free(block);
        pr(" OK\n");
    }
}

uint8_t channelSelect(uint8_t rounds) {  // returns 0 if no accesspoints were found
    const uint8_t __code channelList[6] = {11, 15, 20, 25, 26, 27};
    powerUp(INIT_RADIO);
    int8_t __xdata result[sizeof(channelList)];
    memset(result, 0, sizeof(result));
    for (uint8_t t = 0; t < sizeof(channelList); t++) {
        result[t] = -127;
    }

    for (uint8_t i = 0; i < rounds; i++) {
        for (uint8_t c = 0; c < sizeof(channelList); c++) {
            if (detectAP(channelList[c])) {
                if (lastLQI > result[c]) result[c] = lastLQI;
#ifdef DEBUGMAIN
                pr("MAIN: Channel: %d - LQI: %d RSSI %d\n", channelList[c], lastLQI, lastRSSI);
#endif
            }
        }
    }
    powerDown(INIT_RADIO);
    int8_t __xdata highestLqi = 0;
    int8_t __xdata highestSlot = 0;
    for (uint8_t c = 0; c < sizeof(result); c++) {
        if (result[c] > highestLqi) {
            highestSlot = channelList[c];
            highestLqi = result[c];
        }
    }

    lastLQI = highestLqi;
    return highestSlot;
}

static void getMacAddress(void) {
    for (int i = 0; i < 8; i++) {
        mSelfMac[i] = *((__xdata uint8_t *)(MAC_ADDRESS_LOCATION + i));
    }
}

static void TagAssociated(void) {
    // associated
    __xdata struct AvailDataInfo *__xdata avail;
    // Is there any reason why we should do a long (full) get data request (including reason, status)?
    if ((longDataReqCounter > LONG_DATAREQ_INTERVAL) || wakeUpReason != WAKEUP_REASON_TIMED || secondLongCheckIn) {
        // check if we should do a voltage measurement (those are pretty expensive)
        if (voltageCheckCounter == VOLTAGE_CHECK_INTERVAL) {
            doVoltageReading();
            voltageCheckCounter = 0;
        } else {
            powerUp(INIT_TEMPREADING);
        }
        voltageCheckCounter++;

        // check if the battery level is below minimum, and force a redraw of the screen

        if ((lowBattery && !lowBatteryShown && tagSettings.enableLowBatSymbol) || (noAPShown && tagSettings.enableNoRFSymbol)) {
            // Check if we were already displaying an image
            if (curImgSlot != 0xFF) {
                powerUp(INIT_EEPROM | INIT_EPD);
                // wdt60s();
                drawImageFromEeprom(curImgSlot);
                powerDown(INIT_EEPROM | INIT_EPD);
            } else {
                powerUp(INIT_EPD);
                showSplashScreen();
                powerDown(INIT_EPD);
            }
        }

        powerUp(INIT_RADIO);
        avail = getAvailDataInfo();
        powerDown(INIT_RADIO);

        if (avail != NULL) {
            // we got some data!
            longDataReqCounter = 0;

            if (secondLongCheckIn == true) {
                secondLongCheckIn = false;
            }

            // since we've had succesful contact, and communicated the wakeup reason succesfully, we can now reset to the 'normal' status
            if (wakeUpReason != WAKEUP_REASON_TIMED) {
                secondLongCheckIn = true;
            }
            wakeUpReason = WAKEUP_REASON_TIMED;
        }
        if (tagSettings.enableTagRoaming) {
            uint8_t roamChannel = channelSelect(1);
            if (roamChannel) currentChannel = roamChannel;
        }
    } else {
        powerUp(INIT_RADIO);
        avail = getAvailDataInfo();
        powerDown(INIT_RADIO);
    }

    addAverageValue();

    if (avail == NULL) {
        // no data :( this means no reply from AP
        nextCheckInFromAP = 0;  // let the power-saving algorithm determine the next sleep period
    } else {
        nextCheckInFromAP = avail->nextCheckIn;
        // got some data from the AP!
        if (avail->dataType != DATATYPE_NOUPDATE) {
            // data transfer
            if (processAvailDataInfo(avail)) {
                // succesful transfer, next wake time is determined by the NextCheckin;
            } else {
                // failed transfer, let the algorithm determine next sleep interval (not the AP)
                nextCheckInFromAP = 0;
            }
        } else {
            // no data transfer, just sleep.
        }
        free(avail);
    }

    uint16_t nextCheckin = getNextSleep();
    longDataReqCounter += nextCheckin;

    if (nextCheckin == INTERVAL_AT_MAX_ATTEMPTS) {
        // We've averaged up to the maximum interval, this means the tag hasn't been in contact with an AP for some time.
        if (tagSettings.enableScanForAPAfterTimeout) {
            currentTagMode = TAG_MODE_CHANSEARCH;
            return;
        }
    }

    if (nextCheckInFromAP) {
        // if the AP told us to sleep for a specific period, do so.
        if (nextCheckInFromAP & 0x8000) {
            doSleep((nextCheckInFromAP & 0x7FFF) * 1000UL);
        } else {
            doSleep(nextCheckInFromAP * 60000UL);
        }
    } else {
        // sleep determined by algorithm
        doSleep(getNextSleep() * 1000UL);
    }
}

static void TagChanSearch(void) {
    // not associated
    if (((scanAttempts != 0) && (scanAttempts % VOLTAGEREADING_DURING_SCAN_INTERVAL == 0)) || (scanAttempts > (INTERVAL_1_ATTEMPTS + INTERVAL_2_ATTEMPTS))) {
        doVoltageReading();
    }

    // try to find a working channel
    currentChannel = channelSelect(2);

    // Check if we should redraw the screen with icons, info screen or screensaver
    if ((!currentChannel && !noAPShown && tagSettings.enableNoRFSymbol) ||
        (lowBattery && !lowBatteryShown && tagSettings.enableLowBatSymbol) ||
        (scanAttempts == (INTERVAL_1_ATTEMPTS + INTERVAL_2_ATTEMPTS - 1))) {
        powerUp(INIT_EPD);
        // wdt60s();
        if (curImgSlot != 0xFF) {
            powerUp(INIT_EEPROM);
            drawImageFromEeprom(curImgSlot);
            powerDown(INIT_EEPROM);
        } else if ((scanAttempts >= (INTERVAL_1_ATTEMPTS + INTERVAL_2_ATTEMPTS - 1))) {
            showLongTermSleep();
        } else {
            showSplashScreen();
        }
        powerDown(INIT_EPD);
    }

    // did we find a working channel?
    if (currentChannel) {
        radioSetChannel(currentChannel);
        // now associated! set up and bail out of this loop.
        scanAttempts = 0;
        wakeUpReason = WAKEUP_REASON_NETWORK_SCAN;
        initPowerSaving(INTERVAL_BASE);
        doSleep(getNextSleep() * 1000UL);
        currentTagMode = TAG_MODE_ASSOCIATED;
        return;
    } else {
        // still not associated
        doSleep(getNextScanSleep(true) * 1000UL);
    }
}

#define MAC_ADDRESS_LOCATION 0x780C

void main(void) {
    P1DIR |= (1 << LED);
    P1 |= (1 << LED);

    powerUp(INIT_BASE | INIT_UART);

    pr("Started!\n");

    powerUp(INIT_EEPROM);
    delay_ms(1);
    initializeProto();
    getMacAddress();
    initPowerSaving(INTERVAL_BASE);
    loadSettings();
    powerDown(INIT_EEPROM);
`
    doVoltageReading();

    memtest();

    EA = 1;

    tagSettings.fastBootCapabilities = capabilities;

    // scan for channels
    if (tagSettings.fixedChannel) {
        currentChannel = tagSettings.fixedChannel;
    } else {
        currentChannel = channelSelect(4);
    }

    // currentChannel = 0;

    if (currentChannel) currentTagMode = TAG_MODE_ASSOCIATED;

    P1 &= ~(1 << LED);

    showSplashScreen();

    powerUp(INIT_EEPROM);
    writeSettings();
    powerDown(INIT_EEPROM);

    // powerUp(INIT_RADIO);
    if (currentChannel) radioSetChannel(currentChannel);

    while (1) {
        // wdtPet();
        switch (currentTagMode) {
            case TAG_MODE_ASSOCIATED:
                TagAssociated();
                break;
            case TAG_MODE_CHANSEARCH:
                TagChanSearch();
                break;
        }
    }
}

void executeCommand(uint8_t cmd) {
    switch (cmd) {
        case CMD_DO_REBOOT:
            wdtReset();
            break;
        case CMD_DO_RESET_SETTINGS:
            powerUp(INIT_EEPROM);
            loadDefaultSettings();
            writeSettings();
            powerDown(INIT_EEPROM);
            break;
        case CMD_DO_SCAN:
            currentChannel = channelSelect(4);
            break;
        case CMD_DO_DEEPSLEEP:
            powerUp(INIT_EPD);
            showLongTermSleep();
            powerDown(INIT_EPD | INIT_UART);
            while (1) {
                doSleep(-1);
            }
            break;
        case CMD_ERASE_EEPROM_IMAGES:
            powerUp(INIT_EEPROM);
            //eraseImageBlocks();
            powerDown(INIT_EEPROM);
            break;
        case CMD_GET_BATTERY_VOLTAGE:
            longDataReqCounter = LONG_DATAREQ_INTERVAL + 1;
            voltageCheckCounter = VOLTAGE_CHECK_INTERVAL;
            break;
            /*
#ifndef LEAN_VERSION
        case CMD_ENTER_SLIDESHOW_FAST:
            powerUp(INIT_EEPROM);
            if (findSlotDataTypeArg(CUSTOM_IMAGE_SLIDESHOW << 3) == 0xFF) {
                powerDown(INIT_EEPROM);
                return;
            }
            tagSettings.customMode = TAG_CUSTOM_SLIDESHOW_FAST;
            writeSettings();
            powerDown(INIT_EEPROM);
            wdtDeviceReset();
            break;
        case CMD_ENTER_SLIDESHOW_MEDIUM:
            powerUp(INIT_EEPROM);
            if (findSlotDataTypeArg(CUSTOM_IMAGE_SLIDESHOW << 3) == 0xFF) {
                powerDown(INIT_EEPROM);
                return;
            }
            tagSettings.customMode = TAG_CUSTOM_SLIDESHOW_MEDIUM;
            writeSettings();
            powerDown(INIT_EEPROM);
            wdtDeviceReset();
            break;
        case CMD_ENTER_SLIDESHOW_SLOW:
            powerUp(INIT_EEPROM);
            if (findSlotDataTypeArg(CUSTOM_IMAGE_SLIDESHOW << 3) == 0xFF) {
                powerDown(INIT_EEPROM);
                return;
            }
            tagSettings.customMode = TAG_CUSTOM_SLIDESHOW_SLOW;
            writeSettings();
            powerDown(INIT_EEPROM);
            wdtDeviceReset();
            break;
        case CMD_ENTER_SLIDESHOW_GLACIAL:
            powerUp(INIT_EEPROM);
            if (findSlotDataTypeArg(CUSTOM_IMAGE_SLIDESHOW << 3) == 0xFF) {
                powerDown(INIT_EEPROM);
                return;
            }
            tagSettings.customMode = TAG_CUSTOM_SLIDESHOW_GLACIAL;
            writeSettings();
            powerDown(INIT_EEPROM);
            wdtDeviceReset();
            break;
        case CMD_ENTER_NORMAL_MODE:
            tagSettings.customMode = TAG_CUSTOM_MODE_NONE;
            powerUp(INIT_EEPROM);
            writeSettings();
            powerDown(INIT_EEPROM);
            wdtDeviceReset();
            break;
        case CMD_ENTER_WAIT_RFWAKE:
            tagSettings.customMode = TAG_CUSTOM_MODE_WAIT_RFWAKE;
            powerUp(INIT_EEPROM);
            writeSettings();
            powerDown(INIT_EEPROM);
            wdtDeviceReset();
            break;
            */
        case CMD_LED_NOBLINK:
            ledBlink = false;
            break;
        case CMD_LED_BLINK_1:
            ledBlink = true;
            break;
    }
}
