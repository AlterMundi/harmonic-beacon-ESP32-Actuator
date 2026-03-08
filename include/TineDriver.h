#ifndef TINE_DRIVER_H
#define TINE_DRIVER_H

#include "debug.h"
#include <Arduino.h>

class TineDriver {
private:
  uint8_t pin;
  uint8_t channel;
  float frequency;
  uint8_t dutyCycle;
  String name;
  uint8_t harmonic;
  bool isPlaying;

  unsigned long attackStartTime;
  unsigned long decayStartTime;
  uint32_t attackMs;
  uint32_t decayMs;
  uint32_t pulseDurationMs;
  bool isInitialized;
  bool isEnvelopeActive;

public:
  TineDriver(uint8_t _pin, uint8_t _channel, String _name, uint8_t _harmonic)
      : pin(_pin), channel(_channel), name(_name), harmonic(_harmonic),
        frequency(0), dutyCycle(128), isPlaying(false), isEnvelopeActive(false),
        attackMs(10), decayMs(200), pulseDurationMs(500), isInitialized(false) {
  } // Initialize new member

  // Original init() method
  bool init() {
    isInitialized = true;
    ledcAttachPin(pin, channel);
    DBG_INFO("[TineDriver] %s initialized: pin=%d ch=%d harm=%d\n",
             name.c_str(), pin, channel, harmonic);
    return true;
  }

  void setFrequency(float freq) {
    frequency = freq;
    if (isInitialized && frequency >= 20) {
      ledcSetup(channel, frequency, 8); // 8-bit resolution
      if (isPlaying) {
        ledcWriteTone(channel, frequency);
      }
    }
  }

  void setDuty(uint8_t duty) {
    dutyCycle = duty;
    if (isPlaying) {
      ledcWrite(channel, dutyCycle);
    }
  }

  void setEnvelopeParams(uint16_t attack, uint16_t decay, uint16_t duration) {
    attackMs = attack;
    decayMs = decay;
    pulseDurationMs = duration;
  }

  void playTone(uint8_t velocity, uint32_t durationMs = 0) {
    uint8_t targetDuty = map(velocity, 0, 255, 0, dutyCycle);

    ledcWriteTone(channel, frequency);
    ledcWrite(channel, targetDuty);
    isPlaying = true;

    attackStartTime = millis();
    isEnvelopeActive = (attackMs > 0 || decayMs > 0);

    DBG_VERBOSE("[TineDriver] %s playing: %.2f Hz, duty=%d, dur=%d ms\n",
                name.c_str(), frequency, targetDuty, durationMs);

    // Auto-stop after duration if specified
    if (durationMs > 0) {
      pulseDurationMs = durationMs;
    }
  }

  void pluck(uint16_t pulseMs = 3) {
    ledcWriteTone(channel, frequency);
    ledcWrite(channel, dutyCycle);
    delay(pulseMs);
    ledcWrite(channel, 0);

    DBG_VERBOSE("[TineDriver] %s plucked: %.2f Hz, %d ms\n", name.c_str(),
                frequency, pulseMs);
  }

  void stop() {
    ledcWrite(channel, 0);
    isPlaying = false;
    isEnvelopeActive = false;
    DBG_VERBOSE("[TineDriver] %s stopped\n", name.c_str());
  }

  void update() {
    if (!isPlaying || !isEnvelopeActive)
      return;

    unsigned long now = millis();
    unsigned long elapsed = now - attackStartTime;

    // Attack phase
    if (elapsed < attackMs) {
      uint8_t currentDuty = map(elapsed, 0, attackMs, 0, dutyCycle);
      ledcWrite(channel, currentDuty);
    }
    // Infinite sustain — hold at full duty, no decay/auto-stop
    else if (pulseDurationMs == UINT32_MAX) {
      ledcWrite(channel, dutyCycle);
      isEnvelopeActive = false; // attack done, just hold
    }
    // Timed sustain phase
    else if (elapsed < attackMs + pulseDurationMs) {
      ledcWrite(channel, dutyCycle);
    }
    // Decay phase
    else if (elapsed < attackMs + pulseDurationMs + decayMs) {
      unsigned long decayElapsed = elapsed - (attackMs + pulseDurationMs);
      uint8_t currentDuty = map(decayElapsed, 0, decayMs, dutyCycle, 0);
      ledcWrite(channel, currentDuty);
    }
    // Finished
    else {
      stop();
    }
  }

  // Getters
  float getFrequency() const { return frequency; }
  uint8_t getDuty() const { return dutyCycle; }
  String getName() const { return name; }
  uint8_t getHarmonic() const { return harmonic; }
  bool getIsPlaying() const { return isPlaying; }
  uint8_t getPin() const { return pin; }
  uint8_t getChannel() const { return channel; }
};

#endif // TINE_DRIVER_H
