#include "configFile.h"
#include "constants.h"
#include "debug.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>

// Default tines configuration
static const struct {
  const char *name;
  uint8_t harmonic;
  uint8_t pin;
  uint8_t duty;
} DEFAULT_TINES[] = {
  {"H2-100Hz", 2, 25, 255},
  {"H3-150Hz", 3, 26, 255},
  {"H4-200Hz", 4, 27, 255},
  {"H5-250Hz", 5, 14, 255},
  {"H6-300Hz", 6, 12, 255}
};

static const size_t DEFAULT_TINE_COUNT = sizeof(DEFAULT_TINES) / sizeof(DEFAULT_TINES[0]);

void createConfigFile() {
  if (SPIFFS.exists(CONFIG_FILE_PATH)) {
    return;
  }

  DBG_INFO("Creating default config...\n");

  File file = SPIFFS.open(CONFIG_FILE_PATH, FILE_WRITE);
  if (!file) {
    DBG_ERROR("Failed to open config file for writing\n");
    return;
  }

  JsonDocument config;
  config["device_name"] = "beacon";
  config["fundamental_hz"] = 50.0;

  // Default envelope parameters
  JsonObject params = config["default_params"].to<JsonObject>();
  params["attack_ms"] = 10;
  params["decay_ms"] = 200;
  params["pulse_duration_ms"] = 0;

  // Default tines
  JsonArray tines = config["tines"].to<JsonArray>();
  for (size_t i = 0; i < DEFAULT_TINE_COUNT; i++) {
    JsonObject t = tines.add<JsonObject>();
    t["name"] = DEFAULT_TINES[i].name;
    t["harmonic"] = DEFAULT_TINES[i].harmonic;
    t["pin"] = DEFAULT_TINES[i].pin;
    t["duty"] = DEFAULT_TINES[i].duty;
    t["channel"] = i * 2; // 0, 2, 4, 6, 8
  }

  // OSC Configuration
  config["osc_enabled"] = true;
  config["osc_port"] = 53280;
  config["osc_min_duty"] = 120;
  config["osc_max_duty"] = 255;

  if (serializeJsonPretty(config, file) == 0) {
    DBG_ERROR("Failed to write config\n");
  } else {
    DBG_INFO("Config created successfully\n");
  }

  file.close();
}

String getConfigFile() {
  File file = SPIFFS.open(CONFIG_FILE_PATH, FILE_READ);
  if (!file || file.isDirectory()) {
    return String("{}");
  }
  String content = file.readString();
  file.close();
  return content;
}

JsonDocument loadConfig() {
  JsonDocument doc;

  File file = SPIFFS.open(CONFIG_FILE_PATH, FILE_READ);
  if (!file || file.isDirectory()) {
    DBG_ERROR("Failed to open config file\n");
    return doc;
  }

  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    DBG_ERROR("JSON parse error: %s\n", error.c_str());
  }

  return doc;
}

bool updateConfig(JsonDocument &newConfig) {
  // Remove old config
  if (SPIFFS.exists(CONFIG_FILE_PATH)) {
    SPIFFS.remove(CONFIG_FILE_PATH);
  }

  File file = SPIFFS.open(CONFIG_FILE_PATH, FILE_WRITE);
  if (!file) {
    DBG_ERROR("Failed to open config file for writing\n");
    return false;
  }

  if (serializeJsonPretty(newConfig, file) == 0) {
    DBG_ERROR("Failed to serialize config\n");
    file.close();
    return false;
  }

  file.close();
  DBG_INFO("Config updated\n");
  return true;
}
