# Beacon – Single-Actuator Electromagnet Controller

> **Branch:** `feature/musical-controls` — 2026-02-25

ESP32-based controller for electromagnetic excitation of a single tine/magnet, driven via OSC or HTTP.

## Features

- **Single actuator** — one electromagnet on GPIO 12, LEDC channel 0
- **OSC control** (Surge-compatible) — `/fnote`, `/fnote/rel`, `/allnotesoff`
- **Monophonic** — last-note-wins stack, any note-off stops immediately
- **Configurable OSC** — port, duty range, enable/disable via `/config` or Web UI
- **Web UI** with settings panel (gear icon) for OSC params and envelope
- **Direct HTTP control** — `/play`, `/pluck`, `/stop`
- **OTA Updates** (ArduinoOTA)
- **JSON Configuration** stored in SPIFFS

## Hardware Requirements

- ESP32 Dev Board
- 1× Solenoid / magnetic suction cup (5–12 V)
- 1× Logic-level MOSFET (IRLZ44N, IRLB8721, or similar) on GPIO 12
- 1× Flyback diode (1N4007 or Schottky)
- 1× 100 Ω gate resistor
- Separate 5–12 V power supply; common ground with ESP32

## Quick Start

### PlatformIO Build & Upload

```bash
# Build development firmware
pio run -e dev

# Upload via USB
pio run -e dev --target upload

# Upload via OTA (after first USB flash)
pio run -e dev --target upload --upload-port <ESP32_IP>

# Monitor serial output
pio device monitor -b 115200
```

### Initial Setup

1. Power on ESP32 → creates AP `HarmBcon-{MAC}`
2. Connect to the AP
3. Navigate to **http://192.168.11.1**
4. Configure WiFi via web interface
5. Device reconnects and is available at its assigned IP / `http://harmbeacon.local/`

### Web Interface

Access at `http://<ESP32_IP>/`

- **Gear icon** → Settings modal: OSC port, min/max duty, enable toggle, envelope params
- **Tine button** — direct HTTP play
- **Status** — current frequency and duty

## Configuration

Edit `data/config.json` (uploaded via SPIFFS) or use `POST /config`.

```json
{
  "ssid": "",
  "passwd": "",
  "device_name": "beacon-01",
  "fundamental_hz": 100.0,
  "tines": [
    {"name": "Magnet", "harmonic": 1, "pin": 12, "channel": 0, "duty": 180}
  ],
  "default_params": {
    "pulse_duration_ms": 500,
    "attack_ms": 10,
    "decay_ms": 200,
    "burst_count": 0
  },
  "osc_enabled": true,
  "osc_port": 53280,
  "osc_min_duty": 120,
  "osc_max_duty": 220
}
```

## HTTP API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Web interface |
| `/status` | GET | JSON status |
| `/play` | POST | Play tine (`tine`, `vel`, `dur`) |
| `/pluck` | POST | Percussive pulse (`tine`, `pulse`) |
| `/stop` | POST | Stop all |
| `/config` | GET | View full configuration |
| `/config` | POST | Update configuration (JSON body) |
| `/restart` | POST | Restart ESP32 |

### Examples

```bash
# Read config
curl http://harmbeacon.local/config

# Update OSC port and duty range
curl -X POST http://harmbeacon.local/config \
  -H "Content-Type: application/json" \
  -d '{"osc_port": 53280, "osc_min_duty": 100, "osc_max_duty": 230}'

# Restart
curl -X POST http://harmbeacon.local/restart
```

## OSC Control

Listens on UDP port `53280` by default (configurable via `/config`).

### Addresses

| Address | Args | Description |
|---------|------|-------------|
| `/fnote` | `freq(f)`, `vel(f)`, `[noteID(f)]` | Note-on at exact frequency |
| `/fnote/rel` | `freq(f)`, `vel(f)`, `[noteID(f)]` | Note-off |
| `/allnotesoff` | — | Panic — stops all |

### Parameters

- `freq` — frequency in Hz (20–2000 Hz; out-of-range ignored)
- `vel` — velocity 0–127 (Surge convention); `vel=0` on `/fnote` is treated as note-off
- `noteID` — optional float; used to track polyphonic note identity in the stack

### Velocity → Duty Mapping

Velocity is linearly mapped to PWM duty:

```
duty = osc_min_duty + (vel / 127) × (osc_max_duty - osc_min_duty)
```

### Voice Logic

Monophonic, last-note-wins. Any `/fnote/rel` or `/allnotesoff` stops the actuator immediately.

### Configuration

OSC parameters can be set live via `/config` POST or the Settings modal in the Web UI:

| Field | Description |
|-------|-------------|
| `osc_enabled` | Enable/disable OSC listener |
| `osc_port` | UDP listen port |
| `osc_min_duty` | Duty for vel=0 (0–255) |
| `osc_max_duty` | Duty for vel=127 (0–255) |

## Circuit Diagram

```
ESP32 GPIO 12 ──[100Ω]──→ MOSFET Gate (IRLZ44N)
                            │
                       Drain ──→ Electromagnet (+)
                            │         │
                       Source ──→ GND  VCC (5-12V)
                                       ↓
                              Diode 1N4007 (cathode to VCC)
```

## Project Structure

```
beacon/
├── platformio.ini          # Build configuration
├── min_spiffs.csv          # Partition table (OTA support)
├── data/
│   └── config.json         # Runtime configuration
├── include/
│   ├── TineDriver.h        # Single tine PWM control
│   ├── TineManager.h       # Tine manager
│   ├── OscHandler.h        # OSC UDP listener
│   ├── endpoints.h         # HTTP handlers
│   ├── configFile.h        # SPIFFS config
│   ├── debug.h             # Debug macros
│   └── otaUpdater.h        # OTA support
├── src/
│   ├── main.cpp
│   ├── endpoints.cpp       # Web interface + API
│   ├── OscHandler.cpp      # OSC message handling
│   ├── configFile.cpp      # Config load/save
│   └── otaUpdater.cpp      # ArduinoOTA
└── lib/
    └── WiFiManager/
```

## Memory Usage

- **Flash:** ~979 KB (49.8%)
- **RAM:** ~50 KB (15.4%)

## Troubleshooting

### OTA upload falla con UFW activo

ArduinoOTA abre un puerto efímero en el host para recibir la transferencia de vuelta desde el ESP. Si UFW está activo, puede bloquear esa conexión.

**Solución rápida** — permitir todo tráfico desde el beacon:

```bash
sudo ufw allow from <IP-del-beacon>
```

**Solución específica** — solo el subnet al puerto OTA:

```bash
sudo ufw allow from 10.130.0.0/16 to any port 3232
```

**Solución recomendada** — fijar el puerto OTA:

1. Agregar en `platformio.ini`:

```ini
[env:dev]
upload_flags = --host_port=8266
```

2. Permitir ese puerto en UFW:

```bash
sudo ufw allow 8266/tcp
```

---

## Based On

Architecture inspired by [proyecto-monitoreo](https://github.com/Pablomonte/proyecto-monitoreo)

## License

Same as upstream proyecto-monitoreo
