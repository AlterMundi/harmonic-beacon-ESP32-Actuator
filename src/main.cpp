#include "TineManager.h"
#include "configFile.h"
#include "debug.h"
#include "endpoints.h"
#include "otaUpdater.h"
#include "version.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>

// Global instances
TineManager tineManager;
WebServer server(80);
WiFiManager wifiManager;

#ifdef ENABLE_OTA
bool otaInitialized = false;
#endif

#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

void printBanner() {
  DBG_INFOLN("\n  🧲 BEACON - Electromagnetic Resonator");
  DBG_INFOLN("  ================================\n");
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  DEBUG_BEGIN(115200);
  delay(1000);
  
  printBanner();

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    DBG_ERRORLN("[ERR] SPIFFS mount failed");
  } else {
    DBG_INFOLN("[OK] SPIFFS mounted");
  }

  // Create/load configuration
  createConfigFile();
  JsonDocument config = loadConfig();

  // Initialize tines
  DBG_INFOLN("[INFO] Initializing tines...");
  tineManager.loadFromConfig(config);
  DBG_INFO("[OK] %d tines ready\n", tineManager.getTineCount());

  // Setup HTTP endpoints
  setupEndpoints(server);

  // Setup WiFi Manager
  DBG_INFOLN("[INFO] Starting WiFi Manager...");
  wifiManager.setConnectionTimeout(15000);
  wifiManager.setMaxRetries(5);
  wifiManager.init(&server);

  // Initialize mDNS
  String deviceName = config["device_name"] | "beacon";
  if (MDNS.begin(deviceName.c_str())) {
    DBG_INFO("[OK] mDNS: http://%s.local\n", deviceName.c_str());
    MDNS.addService("http", "tcp", 80);
  }

  server.enableCORS(true);
  server.begin();

  DBG_INFOLN("\n=== SYSTEM READY ===");
  DBG_INFOLN("  API: http://<IP>/api/status");
  DBG_INFOLN("  Web: http://<IP>/");
  DBG_INFOLN("  WiFi: http://192.168.4.1/wifi-setup\n");
}

void loop() {
#ifdef ENABLE_OTA
  if (wifiManager.isOnline() && !isLocalOTAReady()) {
    initLocalOTA(wifiManager.getAPSSID().c_str());
  }
  handleLocalOTA();
#endif

  wifiManager.update();
  server.handleClient();
  tineManager.update();

  delay(1);
}
