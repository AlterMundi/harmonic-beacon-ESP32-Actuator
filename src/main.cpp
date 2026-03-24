#include "OscReceiver.h"
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
OscReceiver oscReceiver(tineManager);

// OSC config (populated in setup(), used for lazy init in loop())
static uint16_t oscPort = 53280;
static bool oscEnabled = true;
static bool oscInitialized = false;

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

  // Read OSC config for use in loop()
  oscPort = config["osc_port"] | 53280;
  oscEnabled = config["osc_enabled"] | true;

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
  DBG_INFOLN("  WiFi: http://192.168.4.1/wifi-setup");
  if (oscEnabled) {
    DBG_INFO("  OSC: UDP port %d (starts after WiFi)\n", oscPort);
  }
}

void loop() {
#ifdef ENABLE_OTA
  if (wifiManager.isOnline() && !isLocalOTAReady()) {
    initLocalOTA(wifiManager.getAPSSID().c_str());
  }
  handleLocalOTA();
#endif

  // Start OSC listener once WiFi is online (lazy init, runs once)
  if (oscEnabled && !oscInitialized && wifiManager.isOnline()) {
    oscReceiver.begin(oscPort);
    oscInitialized = true;
  }

  wifiManager.update();
  server.handleClient();
  oscReceiver.update();
  tineManager.update();

  delay(1);
}
