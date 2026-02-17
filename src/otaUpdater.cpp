#include "otaUpdater.h"
#include "debug.h"
#include <Arduino.h>

// ============== Local OTA (ArduinoOTA) ==============
#ifdef ENABLE_OTA
#include <ArduinoOTA.h>

static bool localOTAInitialized = false;

void initLocalOTA(const char *hostname) {
  if (localOTAInitialized)
    return;

  DBG_INFOLN("\n[INFO] Configuring OTA (lazy init)...");
  ArduinoOTA.setHostname(hostname);

  ArduinoOTA.onStart([]() {
    String type =
        (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    DBG_INFO("[OTA] Start updating %s\n", type.c_str());
  });

  ArduinoOTA.onEnd([]() { DBG_INFOLN("\n[OTA] End"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    DBG_VERBOSE("[OTA] Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    DBG_ERROR("[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      DBG_ERRORLN("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      DBG_ERRORLN("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      DBG_ERRORLN("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      DBG_ERRORLN("Receive Failed");
    else if (error == OTA_END_ERROR)
      DBG_ERRORLN("End Failed");
  });

  ArduinoOTA.begin();
  localOTAInitialized = true;
  DBG_INFOLN("[OK] Local OTA ready on port 3232");
}

void handleLocalOTA() {
  if (localOTAInitialized) {
    ArduinoOTA.handle();
  }
}

bool isLocalOTAReady() { return localOTAInitialized; }

#endif // ENABLE_OTA
