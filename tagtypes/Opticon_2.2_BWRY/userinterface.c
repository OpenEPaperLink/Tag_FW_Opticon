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

bool __xdata lowBatteryShown;
bool __xdata noAPShown;

void showSplashScreen(void) {
    epdPrintBegin();
    epdSetPos(1, 1, 1);
    epdSetScale(2);
    epdPrintf("OpenEPaperLink");

    epdPrintBegin();
    epdSetPos(104, 160, 1);
    epdSetColor(0);
    epdPrintf("2.2\" BW");

    epdPrintBegin();
    epdSetPos(104, 207, 1);
    epdSetColor(1);
    epdPrintf("R");

    epdPrintBegin();
    epdSetPos(104, 214, 1);
    epdSetColor(2);
    epdPrintf("Y");

    epdPrintBegin();
    epdSetPos(86, 2, 1);
    epdSetColor(0);
    uint8_t __xdata mactemp[26];
    spr(mactemp, "%02X:%02X:%02X:", mSelfMac[7], mSelfMac[6], mSelfMac[5]);
    spr(mactemp + 9, "%02X:%02X:%02X:", mSelfMac[4], mSelfMac[3], mSelfMac[2]);
    spr(mactemp + 18, "%02X:%02X", mSelfMac[1], mSelfMac[0]);
    epdPrintf("Tag MAC: %s", mactemp);

    epdDrawImage(opticon);
    epdSetPos(102, 4, 1);
    epdSetScale(1);
    epdSetColor(0);

    if (currentChannel) {
        epdPrintBegin();
        epdSetPos(32, 1, 1);
        epdSetScale(2);
        epdPrintf("AP Found");

        epdDrawImage(receive);
        epdSetPos(15, 196, 1);
        epdSetScale(1);
        epdSetColor(0);

        epdPrintBegin();
        epdSetPos(68, 1, 1);
        epdSetScale(1);
        spr(mactemp, "%02X:%02X:%02X:", APmac[7], APmac[6], APmac[5]);
        spr(mactemp + 9, "%02X:%02X:%02X:", APmac[4], APmac[3], APmac[2]);
        spr(mactemp + 18, "%02X:%02X", APmac[1], APmac[0]);
        epdPrintf("AP: %s Ch: %d", mactemp, currentChannel);
    } else {
        epdPrintBegin();
        epdSetPos(38, 20, 1);
        epdSetScale(2);
        epdPrintf("No AP Found!");

        epdDrawImage(receive);
        epdSetPos(28, 194, 1);
        epdSetScale(1);
        epdSetColor(0);

        epdDrawImage(failed);
        epdSetPos(36, 198, 1);
        epdSetScale(1);
        epdSetColor(1);
    }

    epdDisplay();
}

void addOverlay(void) {
    if (lowBattery) {
        epdDrawImage(battery);
        epdSetPos(118, 234, 1);
        epdSetScale(1);
        epdSetColor(0);
        lowBatteryShown = true;
    } else {
        lowBatteryShown = false;
    }

    if (!currentChannel) {
        epdDrawImage(ant);
        epdSetPos(0, 234, 1);
        epdSetScale(1);
        epdSetColor(0);

        epdDrawImage(cross);
        epdSetPos(8, 242, 1);
        epdSetScale(1);
        epdSetColor(1);
        noAPShown = true;
    } else {
        noAPShown = false;
    }
}

void showLongTermSleep(void) {
    epdDisplay();
}
