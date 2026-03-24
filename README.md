# Beacon — Electromagnetic Kalimba Exciter

ESP32-based controller for electromagnetic excitation of kalimba tines using the natural harmonic series.

## Features

- **5 Tines** controlled via LEDC PWM
- **Natural Harmonic Series** (F×1, F×2, F×3, F×4, F×5)
- **Adjustable Fundamental** frequency via web interface  
- **Envelope Control** (attack/decay) for each tine
- **Web UI** with live controls and melody playback
- **OTA Updates** (ArduinoOTA) for wireless firmware updates
- **Melody Sequencer** with non-blocking playback
- **JSON Configuration** stored in SPIFFS

## Hardware Requirements

### Per Tine
- 1× Magnetic suction cup / solenoid (5-12V)
- 1× Logic-level MOSFET (IRLZ44N, IRLB8721, or similar)
- 1× Flyback diode (1N4007 or Schottky)  
- 1× 100Ω gate resistor

### Power
- ESP32 Dev Board
- Separate 5-12V power supply for electromagnets (~500mA per active tine)
- Common ground between ESP32 and electromagnet power supply

### Default GPIO Mapping
| Tine | Harmonic | GPIO Pin | LEDC Channel |
|------|----------|----------|--------------|
| H1   | ×1       | 25       | 0            |
| H2   | ×2       | 26       | 1            |
| H3   | ×3       | 27       | 2            |
| H4   | ×4       | 14       | 3            |
| H5   | ×5       | 12       | 4            |

## Quick Start

### PlatformIO Environments

| Environment | Upload | Debug | Use case |
|-------------|--------|-------|----------|
| `usb`       | USB (esptool) | Level 2 | First flash / recovery |
| `dev`       | OTA (espota)  | Level 2 | Development iterations |
| `prod`      | USB (esptool) | Off     | Release builds |

### Build & Upload

```bash
# Build
pio run -e dev

# Upload via USB (first time or recovery)
pio run -e usb --target upload

# Upload firmware via OTA
pio run -e dev --target upload --upload-port beacon.local

# Upload filesystem (SPIFFS) via OTA
pio run -e dev --target uploadfs --upload-port beacon.local

# Monitor serial output
pio device monitor -b 115200
```

### OTA + UFW (Firewall) Configuration

ArduinoOTA requires specific ports open on the **PC** (host) firewall for the upload to succeed.

#### Ports Required

| Port | Protocol | Direction | Purpose |
|------|----------|-----------|---------|
| 3232 | TCP      | IN        | `espota.py` host port (PC ← ESP32 data channel) |
| 5353 | UDP      | IN        | mDNS (resolves `beacon.local`) |

#### UFW Commands

```bash
# Allow OTA data channel (required)
sudo ufw allow 3232/tcp comment "PlatformIO OTA (espota)"

# Allow mDNS (required for .local resolution)
sudo ufw allow 5353/udp comment "mDNS / Avahi"

# Verify rules
sudo ufw status numbered
```

#### Troubleshooting OTA

- **`No response from device`** → Verify port 3232/tcp is allowed in UFW and the ESP32 is on the same network.
- **`.local` doesn't resolve** → Check that port 5353/udp is open and `avahi-daemon` is running (`systemctl status avahi-daemon`).
- **OTA times out** → The ESP must have `ArduinoOTA.handle()` running in `loop()`. If the device is busy (sweep, etc.), OTA may fail — stop activity first.
- **Recovery** → If OTA is non-functional, use the `usb` environment: `pio run -e usb --target upload`.

### Initial Setup

1. **Power on ESP32** → creates AP `HarmBcon-{MAC}`
2. **Connect** to the AP from your device
3. Navigate to **http://192.168.11.1**
4. **Configure WiFi** via web interface
5. Device will reconnect and be available at its assigned IP

### Web Interface

Access at `http://<ESP32_IP>/` or `http://harmbeacon.local/`

**Features:**
- **Fundamental slider** — adjusts base frequency (recalculates all harmonics)
- **Tine buttons** — click to play individual tones
- **Melody controls** — play preset sequences ("Scale", "Chord")
- **Status display** — shows current frequencies

## Configuration

Edit `/data/config.json` or use the web interface.

**Example config:**
```json
{
  "fundamental_hz": 64.0,
  "tines": [
    {"name": "H1", "harmonic": 1, "pin": 25, "channel": 0, "duty": 128},
    {"name": "H2", "harmonic": 2, "pin": 26, "channel": 1, "duty": 128}
  ],
  "melodies": {
    "escala": [
      {"tine": 0, "dur": 500, "vel": 200},
      {"tine": 1, "dur": 500, "vel": 200}
    ]
  },
  "default_params": {
    "pulse_duration_ms": 500,
    "attack_ms": 10,
    "decay_ms": 200
  }
}
```

## HTTP API

| Endpoint | Method | Parameters | Description |
|----------|--------|------------|-------------|
| `/` | GET | — | Web interface |
| `/status` | GET | — | JSON status (frequencies, playing state) |
| `/play` | POST | `tine`, `vel`, `dur` | Play tone |
| `/pluck` | POST | `tine`, `pulse` | Percussive pulse |
| `/stop` | POST | — | Stop all tines |
| `/melody` | POST | `name` | Play saved melody |
| `/setfundamental` | POST | `hz` | Change fundamental frequency |
| `/config` | GET | — | View configuration |
| `/config` | POST | JSON body | Update configuration |
| `/restart` | POST | — | Restart ESP32 |

## Circuit Diagram

```
ESP32 GPIO ──[100Ω]──→ MOSFET Gate (IRLZ44N)
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
│   ├── TineManager.h       # Multi-tine manager
│   ├── MelodyPlayer.h      # Non-blocking sequencer
│   ├── endpoints.h         # HTTP handlers
│   ├── debug.h             # Debug macros
│   ├── configFile.h        # SPIFFS config
│   └── otaUpdater.h        # OTA support
├── src/
│   ├── main.cpp            # Setup/loop
│   ├── endpoints.cpp       # Web interface + API
│   ├── configFile.cpp      # Config load/save
│   └── otaUpdater.cpp      # ArduinoOTA
└── lib/
    └── WiFiManager/         # WiFi management
```

## Memory Usage

- **Flash:** 947 KB (48%)
- **RAM:** 50 KB (15%)

## Based On

Architecture inspired by [proyecto-monitoreo](https://github.com/Pablomonte/proyecto-monitoreo)

## License

Same as upstream proyecto-monitoreo
