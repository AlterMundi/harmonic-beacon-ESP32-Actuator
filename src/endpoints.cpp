#include "endpoints.h"
#include "MelodyPlayer.h"
#include "OscHandler.h"
#include "TineManager.h"
#include "configFile.h"
#include "debug.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WebServer.h>

// External references (defined in main.cpp)
extern TineManager tineManager;
extern MelodyPlayer melodyPlayer;
extern OscHandler oscHandler;
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
  // Return a redacted view: passwd omitted, arrays omitted
  JsonDocument doc;
  String raw = getConfigFile();
  if (deserializeJson(doc, raw) != DeserializationError::Ok) {
    server.send(500, "application/json", "{\"ok\":false}");
    return;
  }
  JsonDocument view;
  view["device_name"] = doc["device_name"] | "";
  view["ssid"] = doc["ssid"] | "";
  // passwd intentionally omitted
  view["osc_enabled"] = doc["osc_enabled"] | true;
  view["osc_port"] = doc["osc_port"] | 53280;
  view["osc_min_duty"] = doc["osc_min_duty"] | 120;
  view["osc_max_duty"] = doc["osc_max_duty"] | 220;
  JsonObject vdp = view["default_params"].to<JsonObject>();
  vdp["attack_ms"] = doc["default_params"]["attack_ms"] | 10;
  vdp["decay_ms"] = doc["default_params"]["decay_ms"] | 200;
  vdp["pulse_duration_ms"] = doc["default_params"]["pulse_duration_ms"] | 500;

  String out;
  serializeJson(view, out);
  server.send(200, "application/json", out);
}

void handlePostConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"missing body\"}");
    return;
  }

  JsonDocument in;
  if (deserializeJson(in, server.arg("plain")) != DeserializationError::Ok) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }

  // Load existing config to merge (preserves tines, passwd, melodies, etc.)
  JsonDocument cfg;
  String raw = getConfigFile();
  deserializeJson(cfg, raw);

  bool restart_needed = false;

  // --- OSC fields ---
  if (in["osc_enabled"].is<bool>()) {
    cfg["osc_enabled"] = in["osc_enabled"].as<bool>();
    oscHandler.setEnabled(cfg["osc_enabled"].as<bool>());
  }
  if (in["osc_port"].is<int>()) {
    int np = constrain(in["osc_port"].as<int>(), 1024, 65535);
    if ((int)(cfg["osc_port"] | 53280) != np)
      restart_needed = true;
    cfg["osc_port"] = np;
  }
  if (in["osc_min_duty"].is<int>()) {
    cfg["osc_min_duty"] = (int)constrain(in["osc_min_duty"].as<int>(), 0, 255);
    oscHandler.setMinDuty((uint8_t)cfg["osc_min_duty"].as<int>());
  }
  if (in["osc_max_duty"].is<int>()) {
    cfg["osc_max_duty"] = (int)constrain(in["osc_max_duty"].as<int>(), 0, 255);
    oscHandler.setMaxDuty((uint8_t)cfg["osc_max_duty"].as<int>());
  }
  // enforce min <= max
  if ((int)(cfg["osc_min_duty"] | 0) > (int)(cfg["osc_max_duty"] | 255))
    cfg["osc_min_duty"] = cfg["osc_max_duty"];

  // --- Envelope fields ---
  if (in["default_params"].is<JsonObject>()) {
    JsonObject idp = in["default_params"].as<JsonObject>();
    if (idp["attack_ms"].is<int>())
      cfg["default_params"]["attack_ms"] =
          constrain(idp["attack_ms"].as<int>(), 0, 5000);
    if (idp["decay_ms"].is<int>())
      cfg["default_params"]["decay_ms"] =
          constrain(idp["decay_ms"].as<int>(), 0, 10000);
    if (idp["pulse_duration_ms"].is<int>())
      cfg["default_params"]["pulse_duration_ms"] =
          constrain(idp["pulse_duration_ms"].as<int>(), 0, 10000);
    // Live-apply envelope to TineManager
    tineManager.setEnvelopeDefaults(cfg["default_params"]["attack_ms"] | 10,
                                    cfg["default_params"]["decay_ms"] | 200,
                                    cfg["default_params"]["pulse_duration_ms"] |
                                        500);
  }

  // --- Persist ---
  if (updateConfig(cfg)) {
    String resp = "{\"ok\":true,\"restart_needed\":" +
                  String(restart_needed ? "true" : "false") + "}";
    server.send(200, "application/json", resp);
  } else {
    server.send(500, "application/json",
                "{\"ok\":false,\"error\":\"save failed\"}");
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

  tineManager.pluckNote(tine, pulse);
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
  if (hz < 20 ||
      hz > 2000) { // LEDC min is ~20 Hz; below that the ESP32 crashes
    server.send(400, "text/plain", "Frequency must be 20-2000 Hz");
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

  String mode = server.hasArg("mode") ? server.arg("mode") : "pluck";
  if (mode == "sustain") {
    uint8_t vel = server.hasArg("vel")
                      ? (uint8_t)constrain(server.arg("vel").toInt(), 0, 255)
                      : 200;
    uint32_t dur =
        server.hasArg("dur")
            ? (uint32_t)constrain(server.arg("dur").toInt(), 50, 3000)
            : 500;
    tineManager.playNote(0, vel, dur);
  } else {
    uint16_t pulse =
        server.hasArg("pulse")
            ? (uint16_t)constrain(server.arg("pulse").toInt(), 5, 200)
            : 30;
    tineManager.pluckNote(0, pulse);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST
// /play_freq?hz=<float>&mode=<pluck|sustain>&vel=<0-255>&dur=<ms>&pulse=<ms>
// Plays tine 0 at the given frequency WITHOUT touching the stored fundamental.
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

  String mode_ = server.hasArg("mode") ? server.arg("mode") : "pluck";
  uint8_t vel = server.hasArg("vel")
                    ? (uint8_t)constrain(server.arg("vel").toInt(), 0, 255)
                    : 200;
  uint32_t dur = server.hasArg("dur")
                     ? (uint32_t)constrain(server.arg("dur").toInt(), 50, 3000)
                     : 500;
  uint16_t pulse =
      server.hasArg("pulse")
          ? (uint16_t)constrain(server.arg("pulse").toInt(), 5, 200)
          : 30;

  TineDriver *t0 = tineManager.getTine(0);
  if (t0 == nullptr) {
    server.send(500, "text/plain", "tine 0 not available");
    return;
  }

  t0->setFrequency(hz); // only changes LEDC, not fundamentalHz

  if (mode_ == "sustain") {
    tineManager.playNote(0, vel, dur);
  } else {
    tineManager.pluckNote(0, pulse);
  }
  server.send(200, "text/plain", "OK");
}

// POST /melody_play?name=<string>  — ACK only; sequencing runs in JS
void handleMelodyPlay() {
  String name = server.hasArg("name") ? server.arg("name") : "unknown";
  DBG_INFO("[Melody] Playing: %s (client-side sequencer)\n", name.c_str());
  String resp = "{\"ok\":true,\"name\":\"" + name + "\"}";
  server.send(200, "application/json", resp);
}

void setupEndpoints(WebServer &srv) {
  // Kalimba interface (Home)
  srv.on("/", HTTP_GET, handleHome);
  srv.on("/kalimba", HTTP_GET, handleHome);

  srv.on("/config", HTTP_GET, handleConfig);
  srv.on("/config", HTTP_POST, handlePostConfig);
  srv.on("/settings", HTTP_GET, handleSettings);
  srv.on("/play", HTTP_POST, handlePlay);
  srv.on("/pluck", HTTP_POST, handlePluck);
  srv.on("/stop", HTTP_POST, handleStop);
  srv.on("/melody", HTTP_POST, handleMelody);
  srv.on("/status", HTTP_GET, handleStatus);
  srv.on("/restart", HTTP_POST, handleRestart);
  srv.on("/setfundamental", HTTP_POST, handleSetFundamental);
  srv.on("/setduty", HTTP_POST, handleSetDuty);
  srv.on("/play_hz", HTTP_POST, handlePlayHz);
  srv.on("/play_freq", HTTP_POST, handlePlayFreq);
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
