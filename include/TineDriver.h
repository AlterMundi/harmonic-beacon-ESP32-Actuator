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
  uint8_t currentTargetDuty;
  uint8_t _hpoint; // LEDC hpoint for phase offset (0-255 = 0-360°)
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
  bool isPlucking;
  uint16_t currentPulseMs;

  // Sweep states
  bool isSweeping;
  float sweepStartFreq;
  float sweepEndFreq;
  unsigned long sweepStartTime;
  uint32_t sweepDurationMs;
  float lastQuantizedFreq;

  // Use Arduino ledcWrite consistently — mixing Arduino API with IDF
  // ledc_set_duty_and_update() causes the two layers to fight each other.
  // Phase (hpoint) is not supported until that conflict is resolved.
  void _writeDuty(uint8_t duty) {
    ledcWrite(channel, duty);
  }

public:
  TineDriver(uint8_t _pin, uint8_t _channel, String _name, uint8_t _harmonic)
      : pin(_pin), channel(_channel), name(_name), harmonic(_harmonic),
        frequency(0), dutyCycle(128), currentTargetDuty(128), _hpoint(0),
        isPlaying(false), isEnvelopeActive(false), isPlucking(false),
        currentPulseMs(0), isSweeping(false), sweepStartFreq(0),
        sweepEndFreq(0), sweepStartTime(0), sweepDurationMs(0),
        lastQuantizedFreq(0), attackMs(10), decayMs(200),
        pulseDurationMs(500), isInitialized(false) {}

  bool init() {
    isInitialized = true;
    ledcAttachPin(pin, channel);
    DBG_INFO("[TineDriver] %s initialized: pin=%d ch=%d harm=%d\n",
             name.c_str(), pin, channel, harmonic);
    return true;
  }

  void setFrequency(float freq) {
    if (frequency == freq)
      return;
    frequency = freq;
    if (isInitialized && frequency >= 20) {
      ledcSetup(channel, frequency, 8); // 8-bit resolution
      if (isPlaying) {
        ledcWriteTone(channel, frequency);
        // ledcWriteTone resets hpoint to 0 internally; restore it.
        _writeDuty(currentTargetDuty);
      }
    }
  }

  void setDuty(uint8_t duty) {
    dutyCycle = duty;
    if (isPlaying) {
      _writeDuty(dutyCycle);
    }
  }

  // Set phase offset in degrees (0-360). Applied immediately if playing.
  // hpoint is preserved on every subsequent ledcWrite via _writeDuty().
  void setPhase(float degrees) {
    _hpoint = (uint8_t)(fmod(degrees, 360.0f) / 360.0f * 255.0f);
    if (isPlaying) {
      _writeDuty(currentTargetDuty);
    }
  }

  void setEnvelopeParams(uint16_t attack, uint16_t decay, uint16_t duration) {
    attackMs = attack;
    decayMs = decay;
    pulseDurationMs = duration;
  }

  void playTone(uint8_t velocity, uint32_t durationMs = 0) {
    currentTargetDuty = map(velocity, 0, 255, 0, dutyCycle);

    if (durationMs == 0) {
      pulseDurationMs = UINT32_MAX;
    } else {
      pulseDurationMs = durationMs;
    }

    uint8_t initialDuty = (attackMs > 0) ? 0 : currentTargetDuty;

    if (frequency >= 20) {
      ledcWriteTone(channel, frequency);  // activates PWM at target frequency
    }
    _writeDuty(initialDuty);  // overrides ledcWriteTone's duty=128 with our value
    isPlaying = true;
    isPlucking = false;

    attackStartTime = millis();
    isEnvelopeActive = (attackMs > 0 || decayMs > 0);

    DBG_VERBOSE("[TineDriver] %s playing: %.2f Hz, duty=%d, dur=%d ms\n",
                name.c_str(), frequency, currentTargetDuty, durationMs);
  }

  void pluck(uint16_t pulseMs = 3, uint8_t velocity = 255) {
    currentTargetDuty = map(velocity, 0, 255, 0, dutyCycle);
    if (frequency >= 20) {
      ledcWriteTone(channel, frequency);
    }
    _writeDuty(currentTargetDuty);

    isPlaying = true;
    isPlucking = true;
    isEnvelopeActive = false;
    currentPulseMs = pulseMs;
    attackStartTime = millis();

    DBG_VERBOSE("[TineDriver] %s plucked: %.2f Hz, %d ms\n", name.c_str(),
                frequency, pulseMs);
  }

  void stop() {
    ledcWrite(channel, 0); // hpoint irrelevant when duty=0
    isPlaying = false;
    isEnvelopeActive = false;
    isPlucking = false;
    isSweeping = false;
    DBG_VERBOSE("[TineDriver] %s stopped\n", name.c_str());
  }

  void startSweep(float startHz, float endHz, uint32_t durationMs) {
    if (!isInitialized || durationMs == 0)
      return;

    sweepStartFreq = startHz;
    sweepEndFreq = endHz;
    sweepDurationMs = durationMs;
    sweepStartTime = millis();
    isSweeping = true;
    lastQuantizedFreq = -1.0f;

    currentTargetDuty = dutyCycle;
    _writeDuty(currentTargetDuty);
    isPlaying = true;
    isPlucking = false;
    isEnvelopeActive = false;

    DBG_VERBOSE("[TineDriver] %s Sweep Start: %.2fHz -> %.2fHz over %dms\n",
                name.c_str(), startHz, endHz, durationMs);
  }

  void update() {
    if (!isPlaying)
      return;

    unsigned long now = millis();

    if (isSweeping) {
      unsigned long elapsedSweep = now - sweepStartTime;
      if (elapsedSweep >= sweepDurationMs) {
        setFrequency(sweepEndFreq);
        stop();
        return;
      }

      float progress = (float)elapsedSweep / (float)sweepDurationMs;
      float currentFreq =
          sweepStartFreq + (sweepEndFreq - sweepStartFreq) * progress;
      float quantizedFreq = round(currentFreq * 2.0f) / 2.0f;

      if (quantizedFreq != lastQuantizedFreq) {
        setFrequency(quantizedFreq);
        lastQuantizedFreq = quantizedFreq;
      }
      return;
    }

    unsigned long elapsed = now - attackStartTime;

    if (isPlucking) {
      if (elapsed >= currentPulseMs) {
        stop();
      }
      return;
    }

    if (!isEnvelopeActive)
      return;

    if (elapsed < attackMs) {
      uint8_t currentDuty = map(elapsed, 0, attackMs, 0, currentTargetDuty);
      _writeDuty(currentDuty);
    } else if (pulseDurationMs == UINT32_MAX) {
      _writeDuty(currentTargetDuty);
      isEnvelopeActive = false;
    } else if (elapsed < attackMs + pulseDurationMs) {
      _writeDuty(currentTargetDuty);
    } else if (elapsed < attackMs + pulseDurationMs + decayMs) {
      unsigned long decayElapsed = elapsed - (attackMs + pulseDurationMs);
      uint8_t currentDuty =
          map(decayElapsed, 0, decayMs, currentTargetDuty, 0);
      _writeDuty(currentDuty);
    } else {
      stop();
    }
  }

  // Getters
  float getFrequency() const { return frequency; }
  uint8_t getDuty() const { return dutyCycle; }
  float getPhase() const { return (_hpoint / 255.0f) * 360.0f; }
  String getName() const { return name; }
  uint8_t getHarmonic() const { return harmonic; }
  bool getIsPlaying() const { return isPlaying; }
  uint8_t getPin() const { return pin; }
  uint8_t getChannel() const { return channel; }
  uint32_t getAttackMs() const { return attackMs; }
};

#endif // TINE_DRIVER_H
