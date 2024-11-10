#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

#define FW_VERSION 0x0028          // version number
#define FW_VERSION_SUFFIX "-INIT"  // suffix, like -RC1 or whatever.
// #define DEBUGBLOCKS            // uncomment to enable extra debug information on the block transfers
#define DISABLEWDT  // disables WDT, for testing purposes
// #define DEBUGPROTO  // debug protocol
#define DEBUGOTA    // debug OTA FW updates
// #define DEBUGDRAWING             // debug the drawing part
// #define DEBUGEPD                 // debug the EPD driver
// #define DEBUGMAIN                // parts in the main loop
// #define DEBUGNFC                 // debug NFC functions
//  #define DEBUGGUI                 // debug GUI drawing (enabled)
#define DEBUGSETTINGS            // debug settings module (preferences/eeprom)
//#define DEBUGEEPROM              // eeprom-related debug messages
#define VALIDATE_IMAGE_MD5  // The firmware can validate the image MD5 before displaying it. This costs about 8mAS (milliamp-second) for a 1.54, 16
// #define PRINT_LUT                // uncomment if you want the tag to print the LUT for the current temperature bracket
// #define ENABLE_GPIO_WAKE         // uncomment to enable GPIO wake
// #define ENABLE_RETURN_DATA       // enables the tag to send blocks of data back. Enabling this costs about 4 IRAM bytes
// #define LEAN_VERSION             // makes a smaller version, leaving extra flash space for other things

#ifdef STOCKFWOPTIONS
    // these are rarely used options for writing the mac address to the infopage from flash
    #define WRITE_MAC_FROM_FLASH  // takes mac address from flash if none is set in the infopage
#endif

#if defined(DEBUGSETTINGS) || defined(DEBUGMSG) || defined(DEBUGBLOCKS) || defined(DEBUGPROTO) || defined(DEBUGOTA) || defined(DEBUGNFC) || defined(DEBUGEPD) || defined(DEBUGMAIN) || defined(DEBUGEEPROM)
    #define ISDEBUGBUILD
#endif

#define SETTINGS_STRUCT_VERSION 0x01

#define DEFAULT_SETTING_FASTBOOT 0
#define DEFAULT_SETTING_RFWAKE 0
#define DEFAULT_SETTING_TAGROAMING 0
#define DEFAULT_SETTING_SCANFORAP 1
#define DEFAULT_SETTING_LOWBATSYMBOL 1
#define DEFAULT_SETTING_NORFSYMBOL 1

// power saving algorithm
#define INTERVAL_BASE 35              // interval (in seconds) (when 1 packet is sent/received) for target current (7.2µA)
#define INTERVAL_AT_MAX_ATTEMPTS 600  // interval (in seconds) (at max attempts) for target average current
#define DATA_REQ_RX_WINDOW_SIZE 5UL   // How many milliseconds we should wait for a packet during the data_request.
                                      // If the AP holds a long list of data for tags, it may need a little more time to lookup the mac address
#define DATA_REQ_MAX_ATTEMPTS 10      // How many attempts (at most) we should do to get something back from the AP
#define POWER_SAVING_SMOOTHING 8      // How many samples we should use to smooth the data request interval
#define MAXIMUM_PING_ATTEMPTS 20      // How many attempts to discover an AP the tag should do
#define PING_REPLY_WINDOW 2UL

#define LONG_DATAREQ_INTERVAL 300     // How often (in seconds, approximately) the tag should do a long datareq (including temperature)
#define VOLTAGE_CHECK_INTERVAL 288    // How often the tag should do a battery voltage check (multiplied by LONG_DATAREQ_INTERVAL)
#define BATTERY_VOLTAGE_MINIMUM 2450  // 2600 or below is the best we can do on the EPD

#define MAX_RETURN_DATA_ATTEMPTS 15

// power saving when no AP's were found (scanning every X)
#define VOLTAGEREADING_DURING_SCAN_INTERVAL 2  // how often we should read voltages; this is done every scan attempt in interval bracket 3
#define INTERVAL_1_TIME 3600UL                 // Try every hour
#define INTERVAL_1_ATTEMPTS 24                 // for 24 attempts (an entire day)
#define INTERVAL_2_TIME 7200UL                 // Try every 2 hours
#define INTERVAL_2_ATTEMPTS 12                 // for 12 attempts (an additional day)
#define INTERVAL_3_TIME 86400UL                // Finally, try every day

// slideshow power settings
#define SLIDESHOW_FORCE_FULL_REFRESH_EVERY 16  // force a full refresh every X screen draws
#define SLIDESHOW_INTERVAL_FAST 15             // interval for 'fast'
#define SLIDESHOW_INTERVAL_MEDIUM 60
#define SLIDESHOW_INTERVAL_SLOW 300
#define SLIDESHOW_INTERVAL_GLACIAL 1800

extern struct tagsettings __xdata tagSettings;

extern void loadDefaultSettings(void);
extern void writeSettings(void);
extern void loadSettings(void);
extern void loadSettingsFromBuffer(uint8_t* p);
extern void invalidateSettingsEEPROM(void);
#endif
