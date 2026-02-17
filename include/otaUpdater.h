#ifndef OTA_UPDATER_H
#define OTA_UPDATER_H

#include <Arduino.h>

// Local OTA (ArduinoOTA - development only)
#ifdef ENABLE_OTA
void initLocalOTA(const char *hostname);
void handleLocalOTA();
bool isLocalOTAReady();
#endif

#endif // OTA_UPDATER_H
