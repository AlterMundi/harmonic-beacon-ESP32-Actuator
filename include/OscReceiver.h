#ifndef OSC_RECEIVER_H
#define OSC_RECEIVER_H

#include "TineManager.h"
#include "debug.h"
#include <OSCMessage.h>
#include <WiFiUdp.h>

// OSC protocol for /beacon/* messages:
//   /beacon/play     i(tine_idx) f(vel_0_1) [i(dur_ms, 0=infinite)]
//   /beacon/pluck    i(tine_idx) f(vel_0_1) [i(pulse_ms)]
//   /beacon/stop     i(tine_idx)
//   /beacon/stopall
//   /beacon/fundamental f(hz)
//   /beacon/duty     i(tine_idx) f(duty_0_1)
//   /beacon/phase    i(tine_idx) f(degrees_0_360)

class OscReceiver {
private:
  WiFiUDP _udp;
  TineManager &_tm;
  uint16_t _port;
  bool _started;

public:
  OscReceiver(TineManager &tm)
      : _tm(tm), _port(53280), _started(false) {}

  void begin(uint16_t port) {
    _port = port;
    _udp.begin(port);
    _started = true;
    DBG_INFO("[OSC] Listening on UDP port %d\n", port);
  }

  void stop() {
    _udp.stop();
    _started = false;
  }

  void update() {
    if (!_started) return;

    int size = _udp.parsePacket();
    if (size <= 0) return;

    OSCMessage msg;
    while (size--) {
      msg.fill(_udp.read());
    }

    if (msg.hasError()) {
      DBG_VERBOSE("[OSC] Parse error %d\n", (int)msg.getError());
      return;
    }

    _dispatch(msg);
  }

private:
  void _dispatch(OSCMessage &msg) {
    // /beacon/play i(idx) f(vel_0_1) [i(dur_ms)]
    if (msg.fullMatch("/beacon/play")) {
      if (msg.size() < 2) return;
      uint8_t idx = (uint8_t)msg.getInt(0);
      uint8_t vel = (uint8_t)(constrain(msg.getFloat(1), 0.0f, 1.0f) * 255.0f);
      uint32_t dur = (msg.size() >= 3) ? (uint32_t)msg.getInt(2) : 0;
      _tm.playNote(idx, vel, dur);
      DBG_VERBOSE("[OSC] /beacon/play idx=%d vel=%d dur=%d\n", idx, vel, dur);
      return;
    }

    // /beacon/pluck i(idx) f(vel_0_1) [i(pulse_ms)]
    if (msg.fullMatch("/beacon/pluck")) {
      if (msg.size() < 2) return;
      uint8_t idx = (uint8_t)msg.getInt(0);
      uint8_t vel = (uint8_t)(constrain(msg.getFloat(1), 0.0f, 1.0f) * 255.0f);
      uint16_t pulse = (msg.size() >= 3) ? (uint16_t)msg.getInt(2) : 30;
      _tm.pluckNote(idx, pulse, vel);
      DBG_VERBOSE("[OSC] /beacon/pluck idx=%d vel=%d pulse=%d\n", idx, vel, pulse);
      return;
    }

    // /beacon/stop i(idx)
    if (msg.fullMatch("/beacon/stop")) {
      if (msg.size() < 1) return;
      uint8_t idx = (uint8_t)msg.getInt(0);
      TineDriver *t = _tm.getTine(idx);
      if (t) t->stop();
      DBG_VERBOSE("[OSC] /beacon/stop idx=%d\n", idx);
      return;
    }

    // /beacon/stopall
    if (msg.fullMatch("/beacon/stopall")) {
      _tm.stopAll();
      DBG_VERBOSE("[OSC] /beacon/stopall\n");
      return;
    }

    // /beacon/fundamental f(hz)
    if (msg.fullMatch("/beacon/fundamental")) {
      if (msg.size() < 1) return;
      float hz = msg.getFloat(0);
      if (hz >= 1.0f && hz <= 2000.0f) {
        _tm.setFundamental(hz);
        DBG_VERBOSE("[OSC] /beacon/fundamental %.2f Hz\n", hz);
      }
      return;
    }

    // /beacon/duty i(idx) f(duty_0_1)
    if (msg.fullMatch("/beacon/duty")) {
      if (msg.size() < 2) return;
      uint8_t idx = (uint8_t)msg.getInt(0);
      uint8_t duty = (uint8_t)(constrain(msg.getFloat(1), 0.0f, 1.0f) * 255.0f);
      TineDriver *t = _tm.getTine(idx);
      if (t) t->setDuty(duty);
      DBG_VERBOSE("[OSC] /beacon/duty idx=%d duty=%d\n", idx, duty);
      return;
    }

    // /beacon/phase i(idx) f(degrees_0_360)
    if (msg.fullMatch("/beacon/phase")) {
      if (msg.size() < 2) return;
      uint8_t idx = (uint8_t)msg.getInt(0);
      float deg = msg.getFloat(1);
      TineDriver *t = _tm.getTine(idx);
      if (t) t->setPhase(deg);
      DBG_VERBOSE("[OSC] /beacon/phase idx=%d deg=%.1f\n", idx, deg);
      return;
    }
  }
};

#endif // OSC_RECEIVER_H
