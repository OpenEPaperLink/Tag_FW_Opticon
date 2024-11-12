#include "hal.h"
#include <stdint.h>
#include <stdbool.h>

#include "asmUtil.h"

#define __packed
#include "../shared/oepl-proto.h"
#include "../shared/oepl-definitions.h"

#include <string.h>
#include <stdlib.h>
#include "printf.h"

#include "userinterface.h"

#include "screen.h"

#include "powermgt.h"
#include "proto.h"

#include "drawing.h"
#include "bitmaps.h"

#include "settings.h"

bool __xdata lowBatteryShown;
bool __xdata noAPShown;

void showSplashScreen(void) {
    epdPrintBegin();
    epdSetPos(20, 320, 0);
    epdSetScale(3);
    epdPrintf("OpenEPaperLink");

    epdPrintBegin();
    epdSetPos(263, 50, 0);
    epdSetColor(0);
    epdPrintf("7.4\" BW");

    epdPrintBegin();
    epdSetPos(311, 50, 0);
    epdSetColor(1);
    epdPrintf("R");


    epdPrintBegin();
    epdSetPos(330, 50, 0);
    epdSetColor(0);
    epdPrintf("v%02X%s",FW_VERSION, FW_VERSION_SUFFIX);

    epdPrintBegin();
    epdSetPos(30, 50, 0);
    epdSetColor(0);
    uint8_t __xdata mactemp[26];
    spr(mactemp, "%02X:%02X:%02X:", mSelfMac[7], mSelfMac[6], mSelfMac[5]);
    spr(mactemp + 9, "%02X:%02X:%02X:", mSelfMac[4], mSelfMac[3], mSelfMac[2]);
    spr(mactemp + 18, "%02X:%02X", mSelfMac[1], mSelfMac[0]);
    epdPrintf("Tag MAC: %s", mactemp);

    epdDrawImage(opticon);
    epdSetPos(4, 4, 0);
    epdSetScale(2);
    epdSetColor(0);


    if (currentChannel) {
        epdPrintBegin();
        epdSetPos(100, 140, 0);
        epdSetScale(2);
        epdPrintf("AP Found");

        epdDrawImage(receive);
        epdSetPos(226, 128, 0);
        epdSetScale(1);
        epdSetColor(0);

        epdPrintBegin();
        epdSetPos(30, 70, 0);
        epdSetScale(1);
        spr(mactemp, "%02X:%02X:%02X:", APmac[7], APmac[6], APmac[5]);
        spr(mactemp + 9, "%02X:%02X:%02X:", APmac[4], APmac[3], APmac[2]);
        spr(mactemp + 18, "%02X:%02X", APmac[1], APmac[0]);
        epdPrintf("AP MAC: %s Ch: %d", mactemp, currentChannel);
    } else {
        epdPrintBegin();
        epdSetPos(75, 140, 0);
        epdSetScale(2);
        epdPrintf("No AP Found!");

        epdDrawImage(receive);
        epdSetPos(256, 128, 0);
        epdSetScale(1);
        epdSetColor(0);

        epdDrawImage(failed);
        epdSetPos(262, 128, 0);
        epdSetScale(1);
        epdSetColor(1);
    }
    addOverlay();
    epdDisplay();
}

void addOverlay(void) {
    if (lowBattery) {
        epdDrawImage(battery);
        epdSetPos(612, 0, 0);
        epdSetScale(2);
        epdSetColor(0);
        lowBatteryShown = true;
    }

    if (!currentChannel) {
        epdDrawImage(ant);
        epdSetPos(608, 348, 0);
        epdSetScale(2);
        epdSetColor(0);

        epdDrawImage(cross);
        epdSetPos(624, 348, 0);
        epdSetScale(2);
        epdSetColor(1);
        noAPShown = true;
    }
}

void showLongTermSleep(void) {
        epdDisplay();
}
