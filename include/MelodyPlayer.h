#ifndef MELODY_PLAYER_H
#define MELODY_PLAYER_H

#include "TineManager.h"
#include "debug.h"
#include <ArduinoJson.h>

struct Note {
  uint8_t tineIndex;
  uint32_t durationMs;
  uint8_t velocity;
  uint32_t delayMs; // Delay before this note (for chords/overlaps)
};

class MelodyPlayer {
private:
  TineManager *tineManager;
  std::vector<Note> currentMelody;
  size_t currentNoteIndex;
  unsigned long noteStartTime;
  bool isPlaying;
  bool looping;

public:
  MelodyPlayer(TineManager *tm)
      : tineManager(tm), currentNoteIndex(0), noteStartTime(0),
        isPlaying(false), looping(false) {}

  void loadMelody(JsonArray &sequence) {
    currentMelody.clear();

    for (JsonObject noteObj : sequence) {
      Note n;
      n.tineIndex = noteObj["tine"] | 0;
      n.durationMs = noteObj["dur"] | 500;
      n.velocity = 255; // Force max vel so Slider controls absolute power
      n.delayMs = noteObj["delay"] | 0;

      currentMelody.push_back(n);
    }

    DBG_INFO("[MelodyPlayer] Loaded melody: %d notes\n", currentMelody.size());
  }

  void play(bool loop = false) {
    if (currentMelody.empty()) {
      DBG_ERROR("[MelodyPlayer] No melody loaded\n");
      return;
    }

    currentNoteIndex = 0;
    isPlaying = true;
    looping = loop;
    noteStartTime = millis();

    // Play first note (or chord if delay=0)
    playCurrentNote();

    DBG_INFO("[MelodyPlayer] Started playback (loop=%d)\n", looping);
  }

  void stop() {
    isPlaying = false;
    tineManager->stopAll();
    DBG_VERBOSE("[MelodyPlayer] Stopped\n");
  }

  void update() {
    if (!isPlaying || currentMelody.empty())
      return;

    unsigned long now = millis();
    unsigned long elapsed = now - noteStartTime;

    // Check if current note duration has finished
    if (elapsed >= currentMelody[currentNoteIndex].durationMs) {
      currentNoteIndex++;

      // Check if melody is finished
      if (currentNoteIndex >= currentMelody.size()) {
        if (looping) {
          currentNoteIndex = 0;
          noteStartTime = now;
          playCurrentNote();
          DBG_VERBOSE("[MelodyPlayer] Looping...\n");
        } else {
          isPlaying = false;
          DBG_INFO("[MelodyPlayer] Finished\n");
        }
      } else {
        noteStartTime = now;
        playCurrentNote();
      }
    }
  }

  bool getIsPlaying() const { return isPlaying; }
  size_t getMelodyLength() const { return currentMelody.size(); }

private:
  void playCurrentNote() {
    if (currentNoteIndex >= currentMelody.size())
      return;

    Note &note = currentMelody[currentNoteIndex];

    // Handle delay (for chords - notes starting at same time)
    if (note.delayMs > 0) {
      delay(note.delayMs);
    }

    tineManager->playNote(note.tineIndex, note.velocity, note.durationMs);

    DBG_VERBOSE("[MelodyPlayer] Playing note %d: tine=%d dur=%d vel=%d\n",
                currentNoteIndex, note.tineIndex, note.durationMs,
                note.velocity);
  }
};

#endif // MELODY_PLAYER_H
