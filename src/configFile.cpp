#include "configFile.h"
#include "constants.h"
#include "debug.h"
#include "globals.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WiFi.h>

void createConfigFile() {
  if (SPIFFS.exists(CONFIG_FILE_PATH)) {
    DBG_VERBOSE("Config exists\n");
    return;
  }

  DBG_INFO("Creating config...\n");

  File file = SPIFFS.open(CONFIG_FILE_PATH, FILE_WRITE);
  if (!file) {
    DBG_ERROR("Open config.json failed\n");
    return;
  }

  // Create default Beacon configuration
  JsonDocument config;
  config["fundamental_hz"] = 64.0;

  // Default envelope parameters
  JsonObject params = config["default_params"].to<JsonObject>();
  params["pulse_duration_ms"] = 500;
  params["attack_ms"] = 10;
  params["decay_ms"] = 200;
  params["burst_count"] = 0;

  // Default tines (Harmonic Series)
  JsonArray tines = config["tines"].to<JsonArray>();

  struct TineDef {
    const char *name;
    int harmonic;
    int pin;
    int duty;
  };

  TineDef defaults[] = {{"H6", 6, 25, 128},
                        {"H5", 5, 26, 128},
                        {"H4", 4, 27, 128},
                        {"H3", 3, 14, 128},
                        {"H2", 2, 12, 128}};

  for (const auto &def : defaults) {
    JsonObject t = tines.add<JsonObject>();
    t["name"] = def.name;
    t["harmonic"] = def.harmonic;
    t["pin"] = def.pin;
    t["duty"] = def.duty;
  }

  // Default melodies
  JsonObject melodies = config["melodies"].to<JsonObject>();

  // Scale
  JsonArray escala = melodies["escala"].to<JsonArray>();
  for (int i = 0; i < 5; i++) {
    JsonObject n = escala.add<JsonObject>();
    n["tine"] = i;
    n["dur"] = 1500;
    n["vel"] = 255;
  }

  // Chord
  JsonArray acorde = melodies["acorde"].to<JsonArray>();
  int chordTines[] = {0, 2, 4};
  for (int tIdx : chordTines) {
    JsonObject n = acorde.add<JsonObject>();
    n["tine"] = tIdx;
    n["dur"] = 5000;
    n["vel"] = 255;
    n["delay"] = 0;
  }

  if (serializeJsonPretty(config, file) == 0) {
    DBG_ERROR("Write JSON failed\n");
  } else {
    DBG_INFO("Config created\n");
  }

  file.close();
}

String getConfigFile() {
  File file = SPIFFS.open(CONFIG_FILE_PATH, FILE_READ);
  if (!file || file.isDirectory()) {
    DBG_ERROR("Open config failed\n");
    return String();
  }
  String json = file.readString();
  file.close();
  return json;
}

JsonDocument loadConfig() {
  JsonDocument doc;

  File file = SPIFFS.open(CONFIG_FILE_PATH, FILE_READ);
  if (!file || file.isDirectory()) {
    DBG_ERROR("Open config failed\n");
    return doc;
  }

  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    DBG_ERROR("JSON error: %s\n", error.c_str());
    return doc;
  }

  return doc;
}

bool updateConfig(JsonDocument &newConfig) {
  if (SPIFFS.exists(CONFIG_FILE_PATH)) {
    SPIFFS.remove(CONFIG_FILE_PATH);
  }

  File file = SPIFFS.open(CONFIG_FILE_PATH, FILE_WRITE);
  if (!file) {
    DBG_ERROR("Open config failed\n");
    return false;
  }

  if (serializeJsonPretty(newConfig, file) == 0) {
    DBG_ERROR("Write failed\n");
    file.close();
    return false;
  }

  file.close();
  DBG_INFO("Config updated\n");
  return true;
}
