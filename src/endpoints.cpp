#include "endpoints.h"
#include "MelodyPlayer.h"
#include "TineManager.h"
#include "configFile.h"
#include "debug.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WebServer.h>

// External references (defined in main.cpp)
extern TineManager tineManager;
extern MelodyPlayer melodyPlayer;
extern WebServer server;

void handleRootRedirect() {
  server.sendHeader("Location", "/wifi-setup");
  server.send(302);
}

void handleHome() {
  File file = SPIFFS.open("/index.html", "r");
  if (!file) {
    server.send(500, "text/plain",
                "Error: index.html not found. Please upload filesystem.");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void handleConfig() {
  String config = getConfigFile();
  server.send(200, "application/json", config);
}

void handlePostConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing body");
    return;
  }

  String body = server.arg("plain");
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  if (updateConfig(doc)) {
    server.send(200, "text/plain", "Config updated. Restart to apply.");
  } else {
    server.send(500, "text/plain", "Failed to save config");
  }
}

void handleSettings() {
  server.send(200, "text/html",
              "<html><body><h1>Settings</h1><p>Use /config endpoint for JSON "
              "config</p></body></html>");
}

void handlePlay() {
  uint8_t tine = server.arg("tine").toInt();
  uint8_t vel = server.hasArg("vel") ? server.arg("vel").toInt() : 200;
  uint32_t dur = server.hasArg("dur") ? server.arg("dur").toInt() : 500;

  tineManager.playNote(tine, vel, dur);
  server.send(200, "text/plain", "OK");
}

void handlePluck() {
  uint8_t tine = server.arg("tine").toInt();
  uint16_t pulse = server.hasArg("pulse") ? server.arg("pulse").toInt() : 3;
  uint8_t vel = server.hasArg("vel") ? (uint8_t)constrain(server.arg("vel").toInt(), 0, 255) : 255;

  tineManager.pluckNote(tine, pulse, vel);
  server.send(200, "text/plain", "OK");
}

void handleStop() {
  tineManager.stopAll();
  melodyPlayer.stop();
  server.send(200, "text/plain", "OK");
}

void handleMelody() {
  String name = server.arg("name");
  JsonDocument config = loadConfig();

  if (!config["melodies"][name].is<JsonArray>()) {
    server.send(404, "text/plain", "Melody not found");
    return;
  }

  JsonArray melody = config["melodies"][name];
  melodyPlayer.loadMelody(melody);
  melodyPlayer.play(false);

  server.send(200, "text/plain", "Playing");
}

void handleStatus() {
  JsonDocument doc;
  doc["fundamental_hz"] = tineManager.getFundamental();
  doc["tine_count"] = tineManager.getTineCount();
  doc["is_playing"] = melodyPlayer.getIsPlaying();

  JsonArray tinesArr = doc["tines"].to<JsonArray>();
  for (size_t i = 0; i < tineManager.getTineCount(); i++) {
    TineDriver *t = tineManager.getTine(i);
    if (t) {
      JsonObject tObj = tinesArr.add<JsonObject>();
      tObj["name"] = t->getName();
      tObj["freq"] = t->getFrequency();
      tObj["harmonic"] = t->getHarmonic();
      tObj["is_playing"] = t->getIsPlaying();
    }
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleRestart() {
  server.send(200, "text/plain", "Restarting...");
  delay(500);
  ESP.restart();
}

void handleSetFundamental() {
  float hz = server.arg("hz").toFloat();
  if (hz < 1 || hz > 2000) {
    server.send(400, "text/plain", "Invalid frequency");
    return;
  }

  tineManager.setFundamental(hz);
  server.send(200, "text/plain", "OK");
}

void handleSetDuty() {
  if (!server.hasArg("val")) {
    server.send(400, "text/plain", "Missing val");
    return;
  }
  int val = server.arg("val").toInt();
  if (val < 0 || val > 255)
    val = 128;

  tineManager.setGlobalDuty((uint8_t)val);
  server.send(200, "text/plain", "OK");
}

// POST /play_hz?hz=<float>&mode=<pluck|sustain>&vel=<0-255>&dur=<ms>&pulse=<ms>
// Atomically: setFundamental(hz) + playNote or pluckNote on tine 0
void handlePlayHz() {
  float hz = server.arg("hz").toFloat();
  if (hz < 1 || hz > 2000) {
    server.send(400, "application/json",
                "{\"error\":\"hz out of range [1,2000]\"}");
    return;
  }
  tineManager.setFundamental(hz);

  uint8_t tineIdx = server.hasArg("tine") ? server.arg("tine").toInt() : 0;

  String mode = server.hasArg("mode") ? server.arg("mode") : "pluck";
  uint8_t vel = server.hasArg("vel")
                    ? (uint8_t)constrain(server.arg("vel").toInt(), 0, 255)
                    : 200;
  if (mode == "sustain") {
    uint32_t dur = server.hasArg("dur")
                       ? (uint32_t)constrain(server.arg("dur").toInt(), 0, 3000)
                       : 0;
    tineManager.playNote(tineIdx, vel, dur);
  } else {
    uint16_t pulse =
        server.hasArg("pulse")
            ? (uint16_t)constrain(server.arg("pulse").toInt(), 5, 200)
            : 30;
    tineManager.pluckNote(tineIdx, pulse, vel);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST
// /play_freq?tine=<0-N>&hz=<float>&mode=<pluck|sustain>&vel=<0-255>&dur=<ms>&pulse=<ms>
// Plays the specified tine at the given frequency WITHOUT touching the stored
// fundamental.
static void handlePlayFreq() {
  if (!server.hasArg("hz")) {
    server.send(400, "text/plain", "missing hz");
    return;
  }
  float hz = server.arg("hz").toFloat();
  if (hz < 20.0f) {
    server.send(400, "text/plain", "hz must be >= 20");
    return;
  }

  uint8_t tineIdx = server.hasArg("tine") ? server.arg("tine").toInt() : 0;

  String mode_ = server.hasArg("mode") ? server.arg("mode") : "pluck";
  uint8_t vel = server.hasArg("vel")
                    ? (uint8_t)constrain(server.arg("vel").toInt(), 0, 255)
                    : 200;
  uint32_t dur = server.hasArg("dur")
                     ? (uint32_t)constrain(server.arg("dur").toInt(), 0, 3000)
                     : 0;
  uint16_t pulse =
      server.hasArg("pulse")
          ? (uint16_t)constrain(server.arg("pulse").toInt(), 5, 200)
          : 30;

  TineDriver *t = tineManager.getTine(tineIdx);
  if (t == nullptr) {
    server.send(500, "text/plain", "tine not available");
    return;
  }

  t->setFrequency(hz); // only changes LEDC, not fundamentalHz

  if (mode_ == "sustain") {
    tineManager.playNote(tineIdx, vel, dur);
  } else {
    tineManager.pluckNote(tineIdx, pulse, vel);
  }
  server.send(200, "text/plain", "OK");
}

// POST
// /play_multi
// Expects a JSON payload like:
// {
//   "mode": "sustain",
//   "tines": [
//     {"index": 0, "hz": 64.0, "vel": 255, "dur": 1500, "attack": 20},
//     {"index": 2, "hz": 320.0, "vel": 100, "pulse": 15}
//   ]
// }
static void handlePlayMulti() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json",
                "{\"error\":\"Missing JSON payload\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  String mode_ = doc["mode"] | "pluck";
  JsonArray tines = doc["tines"].as<JsonArray>();

  for (JsonObject tineReq : tines) {
    uint8_t index = tineReq["index"] | 0;

    TineDriver *t = tineManager.getTine(index);
    if (t == nullptr)
      continue;

    if (!tineReq["hz"].isNull()) {
      float hz = tineReq["hz"].as<float>();
      if (hz >= 20.0f) {
        t->setFrequency(hz);
      }
    }

    uint8_t vel = tineReq["vel"] | 200;

    // Apply specific envelope params if provided, otherwise preserve default
    // behavior
    if (!tineReq["attack"].isNull() || !tineReq["dur"].isNull()) {
      uint32_t currentAttack = t->getAttackMs();
      if (!tineReq["attack"].isNull())
        currentAttack = tineReq["attack"].as<uint32_t>();
      // We pass the defaults for decay and pulse duration to avoid resetting
      // them incorrectly here, decay is less critical and pulse is handled by
      // pluck()/playNote() below.
      t->setEnvelopeParams(currentAttack, 200, 500);
    }

    if (mode_ == "sustain") {
      uint32_t dur = tineReq["dur"] | 0;
      tineManager.playNote(index, vel, dur);
    } else {
      uint16_t pulse = tineReq["pulse"] | 30;
      tineManager.pluckNote(index, pulse, vel);
    }
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST
// /sweep
  // Expects JSON:
  // {
  //   "durationMs": 10000,
  //   "tines": [
  //     {"index": 0, "start": 5, "end": 400},
  //     {"index": 1, "start": 15, "end": 1200}
  //   ]
  // }
  static void handleSweep() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json",
                  "{\"error\":\"Missing JSON payload\"}");
      return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    uint32_t durationMs = doc["durationMs"] | 10000;
    JsonArray tines = doc["tines"].as<JsonArray>();

    for (JsonObject tineReq : tines) {
      uint8_t index = tineReq["index"] | 0;

      TineDriver *t = tineManager.getTine(index);
      if (t == nullptr)
        continue;

      if (!tineReq["start"].isNull() && !tineReq["end"].isNull()) {
        float startHz = tineReq["start"].as<float>();
        float endHz = tineReq["end"].as<float>();
        t->startSweep(startHz, endHz, durationMs);
      }
    }

    server.send(200, "application/json", "{\"ok\":true}");
  }

  // POST /melody_play?name=<string>  — ACK only; sequencing runs in JS
  void handleMelodyPlay() {
    String name = server.hasArg("name") ? server.arg("name") : "unknown";
    DBG_INFO("[Melody] Playing: %s (client-side sequencer)\n", name.c_str());
    String resp = "{\"ok\":true,\"name\":\"" + name + "\"}";
    server.send(200, "application/json", resp);
  }

  void setupEndpoints(WebServer & srv) {
    // Kalimba interface (Home)
    srv.on("/", HTTP_GET, handleHome);
    srv.on("/kalimba", HTTP_GET, handleHome);

    srv.on("/config", HTTP_GET, handleConfig);
    srv.on("/config", HTTP_POST, handlePostConfig);
    srv.on("/settings", HTTP_GET, handleSettings);
    srv.on("/play", HTTP_POST, handlePlay);
    srv.on("/pluck", HTTP_POST, handlePluck);
    srv.on("/stop", HTTP_POST, handleStop);
    srv.on("/sweep", HTTP_POST, handleSweep);
    srv.on("/melody", HTTP_POST, handleMelody);
    srv.on("/status", HTTP_GET, handleStatus);
    srv.on("/restart", HTTP_POST, handleRestart);
    srv.on("/setfundamental", HTTP_POST, handleSetFundamental);
    srv.on("/setduty", HTTP_POST, handleSetDuty);
    srv.on("/play_hz", HTTP_POST, handlePlayHz);
    srv.on("/play_freq", HTTP_POST, handlePlayFreq);
    srv.on("/play_multi", HTTP_POST, handlePlayMulti);
    srv.on("/melody_play", HTTP_POST, handleMelodyPlay);

    // Favicon
    srv.on("/favicon.ico", HTTP_GET, []() {
      // Redirect to SVG favicon
      server.sendHeader("Location", "/favicon.svg");
      server.send(301);
    });

    srv.on("/favicon.svg", HTTP_GET, []() {
      File file = SPIFFS.open("/favicon.svg", "r");
      if (!file) {
        server.send(404, "text/plain", "Favicon not found");
        return;
      }
      server.streamFile(file, "image/svg+xml");
      file.close();
    });

    // Standard 404 handler (matching proyecto-monitoreo)
    srv.onNotFound([]() {
      String uri = server.uri();

      // 1. Try to find file in SPIFFS
      if (SPIFFS.exists(uri)) {
        File file = SPIFFS.open(uri, "r");
        String contentType = "text/plain";
        if (uri.endsWith(".css"))
          contentType = "text/css";
        else if (uri.endsWith(".js"))
          contentType = "application/javascript";
        else if (uri.endsWith(".html"))
          contentType = "text/html";
        else if (uri.endsWith(".svg"))
          contentType = "image/svg+xml";
        else if (uri.endsWith(".ico"))
          contentType = "image/x-icon";

        server.streamFile(file, contentType);
        file.close();
        return;
      }

      // Otherwise show the navigation list
      String ip = WiFi.softAPIP().toString();
      String message = "Beacon Controller\n\n";
      message += "You are connected to the Beacon AP.\n";
      message += "Navigate to one of the following:\n\n";
      message += "1. WiFi Setup:  http://" + ip + "/wifi-setup\n";
      message += "2. Kalimba UI:  http://" + ip + "/kalimba\n";
      message += "3. Config:      http://" + ip + "/config\n";

      server.send(404, "text/plain", message);
    });

    DBG_INFOLN("[OK] Application endpoints registered");
  }
