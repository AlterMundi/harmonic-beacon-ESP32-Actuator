#include "OscHandler.h"
#include <ArduinoOSCWiFi.h>

// ─────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────

OscHandler::OscHandler(TineManager *tm)
    : tineManager(tm), notePort(53280), enabled(true), initialized(false),
      stackSize(0), minDuty(120), maxDuty(220), msgCount(0), lastFreq(0),
      lastVel(0) {}

// ─────────────────────────────────────────────
// begin() — subscribe to OSC addresses
// ─────────────────────────────────────────────

void OscHandler::begin(uint16_t port, uint8_t minD, uint8_t maxD) {
  notePort = port;
  minDuty = minD;
  maxDuty = maxD;

  // Subscribe /fnote — note-on at exact frequency
  // Surge format: /fnote freq vel [noteID]  (all floats)
  OscWiFi.subscribe(notePort, "/fnote", [this](const OscMessage &m) {
    if (m.size() < 2)
      return;
    float freq = m.arg<float>(0);
    float vel = m.arg<float>(1);
    int noteID = (m.size() >= 3) ? (int)m.arg<float>(2) : -1;
    this->onFnote(freq, vel, noteID);
  });

  // Subscribe /fnote/rel — note-off
  OscWiFi.subscribe(notePort, "/fnote/rel", [this](const OscMessage &m) {
    if (m.size() < 2)
      return;
    float freq = m.arg<float>(0);
    float vel = m.arg<float>(1);
    int noteID = (m.size() >= 3) ? (int)m.arg<float>(2) : -1;
    this->onFnoteRel(freq, vel, noteID);
  });

  // Subscribe /allnotesoff — panic
  OscWiFi.subscribe(notePort, "/allnotesoff",
                    [this](const OscMessage &) { this->onAllNotesOff(); });

  initialized = true;
  DBG_INFO("[OSC] Listening on port %d for /fnote, /fnote/rel, /allnotesoff\n",
           notePort);
}

// ─────────────────────────────────────────────
// update() — poll in loop()
// ─────────────────────────────────────────────

void OscHandler::update() {
  if (!initialized || !enabled)
    return;
  OscWiFi.update();
}

// ─────────────────────────────────────────────
// OSC Handlers
// ─────────────────────────────────────────────

void OscHandler::onFnote(float freq, float vel, int noteID) {
  msgCount++;
  lastAddress = "/fnote";
  lastFreq = freq;
  lastVel = vel;

  // Clamp frequency
  if (freq < 20.0f || freq > 2000.0f) {
    DBG_VERBOSE("[OSC] /fnote freq %.2f out of range, ignoring\n", freq);
    return;
  }

  // Velocity 0 = note-off (Surge convention)
  if (vel <= 0.001f) {
    onFnoteRel(freq, 0, noteID);
    return;
  }

  DBG_VERBOSE("[OSC] /fnote %.2f Hz vel=%.0f id=%d\n", freq, vel, noteID);

  pushNote(freq, vel, noteID);
  applyTopNote();
}

void OscHandler::onFnoteRel(float freq, float vel, int noteID) {
  msgCount++;
  lastAddress = "/fnote/rel";
  lastFreq = freq;
  lastVel = vel;

  DBG_INFO("[OSC] /fnote/rel %.2f Hz id=%d\n", freq, noteID);

  // Monophonic: any note-off → stop immediately
  stackSize = 0;
  tineManager->stopAll();
}

void OscHandler::onAllNotesOff() {
  msgCount++;
  lastAddress = "/allnotesoff";
  lastFreq = 0;
  lastVel = 0;

  DBG_INFO("[OSC] /allnotesoff — stopping all\n");

  stackSize = 0;
  tineManager->stopAll();
}

// ─────────────────────────────────────────────
// Note stack (last-note-wins, monophonic)
// ─────────────────────────────────────────────

void OscHandler::pushNote(float freq, float vel, int noteID) {
  // Check if this noteID already exists — update it
  for (uint8_t i = 0; i < stackSize; i++) {
    if (noteStack[i].noteID == noteID && noteID >= 0) {
      noteStack[i].freq = freq;
      noteStack[i].vel = vel;
      noteStack[i].timestamp = millis();
      return;
    }
  }

  // Push new note
  if (stackSize < MAX_STACK) {
    noteStack[stackSize] = {freq, vel, noteID, millis(), true};
    stackSize++;
  } else {
    // Stack full — shift left and push at end (drop oldest)
    for (uint8_t i = 0; i < MAX_STACK - 1; i++) {
      noteStack[i] = noteStack[i + 1];
    }
    noteStack[MAX_STACK - 1] = {freq, vel, noteID, millis(), true};
  }
}

void OscHandler::removeNote(int noteID) {
  if (noteID < 0) {
    // No noteID — remove the most recent note
    if (stackSize > 0)
      stackSize--;
    return;
  }

  // Find and remove by noteID
  for (uint8_t i = 0; i < stackSize; i++) {
    if (noteStack[i].noteID == noteID) {
      // Shift remaining notes left
      for (uint8_t j = i; j < stackSize - 1; j++) {
        noteStack[j] = noteStack[j + 1];
      }
      stackSize--;
      return;
    }
  }
}

void OscHandler::applyTopNote() {
  if (stackSize == 0) {
    tineManager->stopAll();
    return;
  }

  // Last note wins (top of stack)
  OscActiveNote &top = noteStack[stackSize - 1];

  // Set frequency directly on tine 0 (without changing fundamental)
  TineDriver *t0 = tineManager->getTine(0);
  if (t0 == nullptr)
    return;

  t0->setFrequency(top.freq);

  // Map velocity to duty (vel is 0-127 in Surge convention)
  uint8_t duty = velToDuty(top.vel);

  // Direct sustain — no envelope, stays on until /fnote/rel or /allnotesoff
  t0->sustainOn(duty);

  DBG_VERBOSE("[OSC] → Playing %.2f Hz duty=%d (stack=%d)\n", top.freq, duty,
              stackSize);
}

// ─────────────────────────────────────────────
// Velocity → Duty mapping
// ─────────────────────────────────────────────

uint8_t OscHandler::velToDuty(float vel_0_127) {
  // Linear mapping: vel [0-127] → duty [minDuty..maxDuty]
  float normalized = constrain(vel_0_127, 0.0f, 127.0f) / 127.0f;
  return (uint8_t)(minDuty + normalized * (maxDuty - minDuty));
}
