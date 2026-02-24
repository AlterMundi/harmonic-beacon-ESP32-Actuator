#ifndef OSC_HANDLER_H
#define OSC_HANDLER_H

#include "TineManager.h"
#include "debug.h"
#include <Arduino.h>

// Forward declaration — ArduinoOSC included only in .cpp
// This keeps the header lightweight

struct OscActiveNote {
  float freq;
  float vel; // 0-127 (Surge convention)
  int noteID;
  uint32_t timestamp;
  bool active;
};

class OscHandler {
private:
  TineManager *tineManager;
  uint16_t notePort;
  bool enabled;
  bool initialized;

  // Monophonic: last-note-wins with stack
  static const uint8_t MAX_STACK = 8;
  OscActiveNote noteStack[MAX_STACK];
  uint8_t stackSize;

  // Duty clamps
  uint8_t minDuty;
  uint8_t maxDuty;

  // Telemetry
  volatile uint32_t msgCount;
  String lastAddress;
  float lastFreq;
  float lastVel;

  // Internal helpers
  uint8_t velToDuty(float vel_0_127);
  void applyTopNote();
  void pushNote(float freq, float vel, int noteID);
  void removeNote(int noteID);

public:
  OscHandler(TineManager *tm);

  void begin(uint16_t port = 53280, uint8_t minD = 120, uint8_t maxD = 220);
  void update();

  void onFnote(float freq, float vel, int noteID);
  void onFnoteRel(float freq, float vel, int noteID);
  void onAllNotesOff();

  // Setters
  void setEnabled(bool en) { enabled = en; }
  void setMinDuty(uint8_t d) { minDuty = d; }
  void setMaxDuty(uint8_t d) { maxDuty = d; }

  // Getters for status
  bool isEnabled() const { return enabled; }
  bool isInitialized() const { return initialized; }
  uint32_t getMessageCount() const { return msgCount; }
  String getLastAddress() const { return lastAddress; }
  float getLastFreq() const { return lastFreq; }
  float getLastVel() const { return lastVel; }
  uint8_t getActiveCount() const { return stackSize; }
};

#endif // OSC_HANDLER_H
