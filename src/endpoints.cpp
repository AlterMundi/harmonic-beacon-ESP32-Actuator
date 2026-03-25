#include "endpoints.h"
#include "TineManager.h"
#include "configFile.h"
#include "debug.h"
#include "version.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WebServer.h>

extern TineManager tineManager;
extern WebServer server;

// Static file serving
void serveFile(const char *path, const char *contentType) {
  File file = SPIFFS.open(path, "r");
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.streamFile(file, contentType);
  file.close();
}

// GET /api/status - System state
void handleStatus() {
  JsonDocument doc;
  doc["fundamental_hz"] = tineManager.getFundamental();
  doc["tine_count"] = tineManager.getTineCount();

  JsonArray tines = doc["tines"].to<JsonArray>();
  for (size_t i = 0; i < tineManager.getTineCount(); i++) {
    TineDriver *t = tineManager.getTine(i);
    if (t) {
      JsonObject obj = tines.add<JsonObject>();
      obj["index"] = i;
      obj["name"] = t->getName();
      obj["freq"] = t->getFrequency();
      obj["harmonic"] = t->getHarmonic();
      obj["is_playing"] = t->getIsPlaying();
      obj["phase_deg"] = t->getPhase();
    }
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// GET /api/config - Current configuration
void handleGetConfig() {
  String config = getConfigFile();
  server.send(200, "application/json", config);
}

// POST /api/config - Update configuration
void handlePostConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  // Load existing config to merge
  JsonDocument currentConfig = loadConfig();
  
  // Merge new values into current config
  if (doc["device_name"].is<const char*>()) {
    currentConfig["device_name"] = doc["device_name"];
  }
  if (doc["fundamental_hz"].is<float>()) {
    currentConfig["fundamental_hz"] = doc["fundamental_hz"];
  }
  if (doc["tines"].is<JsonArray>()) {
    currentConfig["tines"] = doc["tines"];
  }
  if (doc["default_params"].is<JsonObject>()) {
    currentConfig["default_params"] = doc["default_params"];
  }
  
  // OSC Settings
  if (doc["osc_enabled"].is<bool>()) {
    currentConfig["osc_enabled"] = doc["osc_enabled"];
  }
  if (doc["osc_port"].is<int>()) {
    currentConfig["osc_port"] = doc["osc_port"];
  }
  if (doc["osc_min_duty"].is<int>()) {
    currentConfig["osc_min_duty"] = doc["osc_min_duty"];
  }
  if (doc["osc_max_duty"].is<int>()) {
    currentConfig["osc_max_duty"] = doc["osc_max_duty"];
  }

  // Check if OSC port changed (requires restart)
  int oldPort = loadConfig()["osc_port"] | 53280;
  int newPort = doc["osc_port"] | oldPort;
  bool restartNeeded = (oldPort != newPort);

  if (updateConfig(currentConfig)) {
    // Reload tines with new config if tines changed
    if (doc["tines"].is<JsonArray>() || doc["fundamental_hz"].is<float>()) {
      tineManager.loadFromConfig(currentConfig);
    }
    
    JsonDocument response;
    response["ok"] = true;
    response["restart_needed"] = restartNeeded;
    
    String respStr;
    serializeJson(response, respStr);
    server.send(200, "application/json", respStr);
  } else {
    server.send(500, "application/json", "{\"error\":\"Failed to save config\"}");
  }
}

// POST /api/play - Unified play endpoint
// Body: {"mode":"pluck|sustain","tines":[{"index":0,"hz":64,"vel":255,"pulse":20,"phase":90.0}]}
void handlePlay() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const char *mode = doc["mode"] | "pluck";
  JsonArray tines = doc["tines"].as<JsonArray>();

  if (!tines || tines.size() == 0) {
    server.send(400, "application/json", "{\"error\":\"No tines specified\"}");
    return;
  }

  for (JsonObject t : tines) {
    uint8_t idx = t["index"] | 0;
    float hz = t["hz"];
    uint8_t vel = t["vel"] | 200;

    TineDriver *td = tineManager.getTine(idx);
    if (!td) continue;

    // Set frequency if specified
    if (hz >= 20.0f) {
      td->setFrequency(hz);
    }

    if (strcmp(mode, "sustain") == 0) {
      uint32_t dur = t["dur"] | 0;
      if (td->getIsPlaying() && dur == 0) {
        // Tine already sustaining — update duty smoothly without restarting PWM.
        td->updateTarget(vel);
      } else {
        uint32_t atk = t["attack"] | 10;
        td->setEnvelopeParams(atk, 200, dur);
        tineManager.playNote(idx, vel, dur);
      }
    } else {
      uint16_t pulse = t["pulse"] | 30;
      tineManager.pluckNote(idx, pulse, vel);
    }

    // Apply phase offset after play (setPhase re-applies hpoint via IDF)
    if (t["phase"].is<float>()) {
      td->setPhase(t["phase"].as<float>());
    }
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /api/stop - Stop all
void handleStop() {
  tineManager.stopAll();
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /api/sweep - Frequency sweep
// Body: {"duration_ms":10000,"tines":[{"index":0,"start":10,"end":400}]}
void handleSweep() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  uint32_t duration = doc["duration_ms"] | 10000;
  JsonArray tines = doc["tines"].as<JsonArray>();

  if (!tines) {
    server.send(400, "application/json", "{\"error\":\"No sweep data\"}");
    return;
  }

  for (JsonObject t : tines) {
    uint8_t idx = t["index"] | 0;
    float start = t["start"] | 10.0f;
    float end = t["end"] | 400.0f;

    TineDriver *td = tineManager.getTine(idx);
    if (td) {
      td->startSweep(start, end, duration);
    }
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /api/fundamental - Update fundamental frequency
void handleSetFundamental() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  float hz = doc["hz"] | 0.0f;
  if (hz < 1.0f || hz > 2000.0f) {
    server.send(400, "application/json", "{\"error\":\"Invalid frequency\"}");
    return;
  }

  tineManager.setFundamental(hz);
  server.send(200, "application/json", "{\"ok\":true}");
}

// GET /api/version
void handleVersion() {
  server.send(200, "application/json",
              "{\"version\":\"" FIRMWARE_VERSION "\",\"features\":[\"osc\",\"phase\"]}");
}

void setupEndpoints(WebServer &srv) {
  // API endpoints
  srv.on("/api/version", HTTP_GET, handleVersion);
  srv.on("/api/status", HTTP_GET, handleStatus);
  srv.on("/api/config", HTTP_GET, handleGetConfig);
  srv.on("/api/config", HTTP_POST, handlePostConfig);
  srv.on("/api/play", HTTP_POST, handlePlay);
  srv.on("/api/stop", HTTP_POST, handleStop);
  srv.on("/api/sweep", HTTP_POST, handleSweep);
  srv.on("/api/fundamental", HTTP_POST, handleSetFundamental);

  // Static files
  srv.on("/", HTTP_GET, []() { serveFile("/index.html", "text/html"); });
  srv.on("/favicon.svg", HTTP_GET, []() { serveFile("/favicon.svg", "image/svg+xml"); });

  // 404 handler
  srv.onNotFound([]() {
    server.send(404, "application/json", "{\"error\":\"Not found\"}");
  });

  DBG_INFOLN("[OK] API endpoints registered");
}
