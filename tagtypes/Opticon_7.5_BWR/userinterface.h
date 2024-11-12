#ifndef USER_INTERFACE_H
#define USER_INTERFACE_H

void addOverlay(void);

void showSplashScreen(void);

void showLongTermSleep(void);

extern const uint16_t __code fwVersion;
extern const char __code fwVersionSuffix[];
extern bool __xdata lowBatteryShown;
extern bool __xdata noAPShown;

#endif