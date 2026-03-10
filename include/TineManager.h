#ifndef TINE_MANAGER_H
#define TINE_MANAGER_H

#include "TineDriver.h"
#include "debug.h"
#include <ArduinoJson.h>
#include <vector>

class TineManager {
private:
  std::vector<TineDriver *> tines;
  float fundamentalHz;
  uint32_t defaultAttackMs;
  uint32_t defaultDecayMs;
  uint32_t defaultPulseDurationMs;

public:
  TineManager()
      : fundamentalHz(64.0), defaultAttackMs(10), defaultDecayMs(200),
        defaultPulseDurationMs(500) {}

  ~TineManager() {
    for (auto *t : tines) {
      delete t;
    }
  }

  void loadFromConfig(JsonDocument &config) {
    // Clear existing tines
    for (auto *t : tines) {
      delete t;
    }
    tines.clear();

    // Load fundamental frequency
    fundamentalHz = config["fundamental_hz"] | 64.0;
    DBG_INFO("[TineManager] Fundamental: %.2f Hz\n", fundamentalHz);

    // Load default envelope params
    if (config["default_params"].is<JsonObject>()) {
      JsonObject params = config["default_params"];
      defaultAttackMs = params["attack_ms"] | 10;
      defaultDecayMs = params["decay_ms"] | 200;
      defaultPulseDurationMs = params["pulse_duration_ms"] | 500;
      DBG_VERBOSE("[TineManager] Envelope: attack=%d decay=%d dur=%d ms\n",
                  defaultAttackMs, defaultDecayMs, defaultPulseDurationMs);
    }

    // Load tines
    if (!config["tines"].is<JsonArray>()) {
      DBG_ERROR("[TineManager] No tines in config\n");
      return;
    }

    JsonArray tineArray = config["tines"];
    for (JsonObject tineCfg : tineArray) {
      String name = tineCfg["name"] | "Unknown";
      uint8_t harmonic = tineCfg["harmonic"] | 1;
      uint8_t pin = tineCfg["pin"] | 0;
      uint8_t channel;
      if (tineCfg.containsKey("channel")) {
        channel = tineCfg["channel"].as<uint8_t>();
      } else {
        channel = tines.size() * 2; // Channels 0, 2, 4 use distinct timers
      }
      uint8_t duty = tineCfg["duty"] | 128;

      if (pin == 0 || channel > 15) {
        DBG_ERROR("[TineManager] Invalid config for %s (pin=%d ch=%d)\n",
                  name.c_str(), pin, channel);
        continue;
      }

      TineDriver *tine = new TineDriver(pin, channel, name, harmonic);
      tine->setDuty(duty);
      tine->setEnvelopeParams(defaultAttackMs, defaultDecayMs,
                              defaultPulseDurationMs);

      if (tine->init()) {
        // Calculate frequency from harmonic series
        float freq = fundamentalHz * harmonic;
        tine->setFrequency(freq);
        tines.push_back(tine);
        DBG_INFO("[TineManager] Added %s: H%d = %.2f Hz (pin %d, ch %d)\n",
                 name.c_str(), harmonic, freq, pin, channel);
      } else {
        delete tine;
        DBG_ERROR("[TineManager] Failed to init %s\n", name.c_str());
      }
    }

    DBG_INFO("[TineManager] Loaded %d tines\n", tines.size());
  }

  void setFundamental(float hz) {
    fundamentalHz = hz;
    DBG_INFO("[TineManager] Fundamental changed to %.2f Hz\n", fundamentalHz);

    // Recalculate all frequencies
    for (auto *tine : tines) {
      float newFreq = fundamentalHz * tine->getHarmonic();
      tine->setFrequency(newFreq);
      DBG_VERBOSE("[TineManager] %s: %.2f Hz\n", tine->getName().c_str(),
                  newFreq);
    }
  }

  void setGlobalDuty(uint8_t duty) {
    for (auto *tine : tines) {
      tine->setDuty(duty);
    }
    DBG_INFO("[TineManager] Global duty set to %d\n", duty);
  }

  void playNote(uint8_t index, uint8_t velocity, uint32_t durationMs = 0) {
    if (index >= tines.size()) {
      DBG_ERROR("[TineManager] Invalid tine index %d\n", index);
      return;
    }
    tines[index]->playTone(velocity, durationMs);
  }

  void pluckNote(uint8_t index, uint16_t pulseMs = 3, uint8_t velocity = 255) {
    if (index >= tines.size()) {
      DBG_ERROR("[TineManager] Invalid tine index %d\n", index);
      return;
    }
    tines[index]->pluck(pulseMs, velocity);
  }

  void stopAll() {
    for (auto *tine : tines) {
      tine->stop();
    }
    DBG_VERBOSE("[TineManager] All tines stopped\n");
  }

  void update() {
    for (auto *tine : tines) {
      tine->update();
    }
  }

  // Getters
  std::vector<TineDriver *> &getTines() { return tines; }
  float getFundamental() const { return fundamentalHz; }
  size_t getTineCount() const { return tines.size(); }
  TineDriver *getTine(uint8_t index) {
    if (index < tines.size())
      return tines[index];
    return nullptr;
  }
};

#endif // TINE_MANAGER_H
