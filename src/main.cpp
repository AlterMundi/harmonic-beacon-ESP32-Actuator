#include "MelodyPlayer.h"
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
MelodyPlayer melodyPlayer(&tineManager);
WebServer server(80);
WiFiManager wifiManager;

#ifdef ENABLE_OTA
bool otaInitialized = false;
#endif

void printBanner() {
  DBG_INFOLN("\n  🎵 BEACON - Electromagnetic Kalimba");
  DBG_INFOLN("  Harmonic Series Controller\n");
}

void setup() {
  DEBUG_BEGIN(115200);
  delay(1000); // Wait for serial to stabilize
  Serial.flush();
  Serial.println("\n\n=== BOOT ===");

  IF_INFO(printBanner());

  DBG_INFOLN("[INFO] Starting system...");

  // Mount SPIFFS
  if (!SPIFFS.begin(true)) {
    DBG_ERRORLN("[ERR] SPIFFS mount failed");
  } else {
    DBG_INFOLN("[OK] SPIFFS mounted");
  }

  // Create config file if needed
  createConfigFile();

  // Load configuration
  JsonDocument config = loadConfig();

  IF_VERBOSE({
    DBG_INFOLN("\n[INFO] Config loaded:");
    String configOutput;
    serializeJsonPretty(config, configOutput);
    DBG_INFOLN(configOutput.c_str());
  });

  // Initialize tines
  DBG_INFOLN("\n[INFO] Initializing tines...");
  tineManager.loadFromConfig(config);
  DBG_INFO("[OK] %d tines ready\n", tineManager.getTineCount());

  // Setup HTTP endpoints first (like proyecto-monitoreo)
  setupEndpoints(server);

  // Setup WiFi Manager
  DBG_INFOLN("\n[INFO] Configuring WiFi Manager...");
  wifiManager.setConnectionTimeout(15000);
  wifiManager.setMaxRetries(8);
  wifiManager.setValidationTimeout(30000);
  wifiManager.init(&server);
  DBG_INFOLN("[OK] WiFi Manager initialized");

  // Initialize MDNS
  if (MDNS.begin("harmbeacon")) {
    DBG_INFOLN("[OK] MDNS responder started: http://harmbeacon.local");
    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);
  } else {
    DBG_ERRORLN("[ERR] Error setting up MDNS responder!");
  }

  server.enableCORS(true);
  server.begin();
  DBG_INFOLN("[OK] Web server started on port 80");

#ifdef ENABLE_OTA
  DBG_INFOLN("[INFO] OTA will initialize when WiFi connects");
#endif

  DBG_INFOLN("\n=== SYSTEM READY ===");
  DBG_INFO("  AP: %s\n", wifiManager.getAPSSID().c_str());
  DBG_INFOLN("  Config: http://192.168.4.1");
  DBG_INFOLN("  Control: http://<IP>/\n");
}

void loop() {
#ifdef ENABLE_OTA
  // Lazy OTA initialization - wait for WiFi to be connected
  if (wifiManager.isOnline() && !isLocalOTAReady()) {
    initLocalOTA(wifiManager.getAPSSID().c_str());
  }
  handleLocalOTA();
#endif

  wifiManager.update();
  server.handleClient();

  // Update tines (envelope processing)
  tineManager.update();

  // Update melody player (non-blocking)
  melodyPlayer.update();

  delay(1);
}
