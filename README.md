# 💧 ESP32 Smart Water Tank Management System

A fully featured water tank monitoring and automatic pump control system built on the **ESP32** microcontroller. Monitor your tank level in real-time from any browser on your phone or laptop — no app install needed.

![Platform](https://img.shields.io/badge/Platform-ESP32-blue?logo=espressif)
![Arduino](https://img.shields.io/badge/IDE-Arduino-teal?logo=arduino)
![License](https://img.shields.io/badge/License-MIT-green)
![WiFi](https://img.shields.io/badge/WiFi-AP%20%7C%20STA-orange)
![No Extra Libs](https://img.shields.io/badge/Libraries-None%20extra-brightgreen)

---

## 📋 Table of Contents

- [Features](#-features)
- [Hardware Required](#-hardware-required)
- [Wiring Diagram](#-wiring-diagram)
- [How It Works](#-how-it-works)
- [Getting Started](#-getting-started)
- [Configuration](#️-configuration)
- [Web Dashboard](#-web-dashboard)
- [JSON API](#-json-api)
- [Serial Monitor Commands](#-serial-monitor-commands)
- [Auto vs Manual Mode](#-auto-vs-manual-mode)
- [Buzzer Alert Logic](#-buzzer-alert-logic)
- [Troubleshooting](#-troubleshooting)
- [Project Structure](#-project-structure)
- [Contributing](#-contributing)
- [License](#-license)

---

## ✨ Features

| Feature | Details |
|---|---|
| 📡 Ultrasonic sensing | HC-SR04 with 5-sample averaging for stable readings |
| 💧 Water level | Displayed as %, cm height, and estimated litres |
| ⚙️ Automatic pump control | Pump ON/OFF triggered by configurable thresholds |
| 🖐 Manual override | Control pump directly from the web dashboard |
| 🔔 Buzzer alerts | Fast beep = critical low, slow beep = overflow |
| 🌐 Wi-Fi dashboard | Animated tank, live stats, controls — no app needed |
| 📊 JSON API | `/data` endpoint for custom integrations or home automation |
| 🔌 AP & STA Wi-Fi | Hotspot mode (no router) or connects to your home Wi-Fi |
| 🖥️ Serial commands | Control and monitor via Serial monitor |
| 🔁 Non-blocking code | Buzzer, sensor, and server all run without `delay()` conflicts |

---

## 🧰 Hardware Required

| Component | Quantity | Notes |
|---|---|---|
| ESP32 Dev Board | 1 | Any 30-pin or 38-pin variant |
| HC-SR04 Ultrasonic Sensor | 1 | 5 V module — use voltage divider on ECHO pin if needed |
| Relay Module (active-LOW) | 1 | Single-channel, 5 V coil, for controlling AC/DC pump |
| Buzzer | 1 | Active buzzer (3.3 V or 5 V) |
| Water Pump | 1 | Matched to your tank — AC or DC via relay |
| Jumper Wires | — | Male-to-male and male-to-female |
| Power Supply | 1 | 5 V/2 A USB or dedicated supply for ESP32 |

> **⚠️ Note on HC-SR04 ECHO pin:** The HC-SR04 outputs 5 V on ECHO. ESP32 GPIO is 3.3 V tolerant. Use a **voltage divider** (1 kΩ + 2 kΩ) or a logic level converter on the ECHO line to be safe.

---

## 🔌 Wiring Diagram

```
                          ┌──────────────────┐
                          │   ESP32 Dev Board│
                          │                  │
  HC-SR04                 │  GPIO 5  ────────┼──── TRIG
  ┌────────┐              │  GPIO 18 ────────┼──── ECHO  (via divider!)
  │  TRIG  │◄─────────────┤                  │
  │  ECHO  │──────────────┤                  │
  │  VCC   │◄── 5V        │  GPIO 26 ────────┼──── Relay IN
  │  GND   │◄── GND       │  GPIO 27 ────────┼──── Buzzer +
  └────────┘              │                  │
                          │  GND     ────────┼──── Relay GND
  Relay Module            │  5V / VIN────────┼──── Relay VCC
  ┌────────────┐          │                  │
  │  IN   ◄────┼──────────┤  GPIO 26         │
  │  VCC  ◄────┼──────────┤  5V / VIN        │
  │  GND  ◄────┼──────────┤  GND             │
  │  COM  ──── Pump Line  │                  │
  │  NO   ──── Pump Line  │                  │
  └────────────┘          └──────────────────┘

  Buzzer
  ┌──────┐
  │  +   │◄──── GPIO 27
  │  -   │◄──── GND
  └──────┘
```

**Relay wiring for the pump:**
- Connect **COM** to one terminal of your power supply going to the pump.
- Connect **NO** (Normally Open) to the pump's power terminal.
- When the relay activates, the circuit closes and the pump runs.

> **⚡ Safety:** Never wire mains AC voltage yourself unless you are qualified. Use a DC pump with an appropriate DC relay if you are not comfortable with AC wiring.

---

## 🔍 How It Works

```
┌─────────────────────────────────────────────────────────────────┐
│                         SYSTEM FLOW                             │
│                                                                 │
│  HC-SR04          ESP32                  Relay        Pump      │
│  ────────         ─────────────────      ─────        ─────     │
│  Measure   ──►   Calculate level %  ──► ON/OFF  ──►  ON/OFF    │
│  distance         Check thresholds                              │
│                   Update web server                             │
│                   Trigger buzzer                                │
│                   Serve JSON API                                │
└─────────────────────────────────────────────────────────────────┘

  Sensor distance  →  water height  →  level %  →  litres

  level % = (waterHeight / TANK_TOTAL_CM) × 100
  litres  = (LENGTH × WIDTH × waterHeight) / 1000
```

The sensor sits at the **top of the tank** pointing straight down. It measures the distance to the water surface. The firmware subtracts that from the total tank depth to get the water height, then converts to a percentage and volume.

---

## 🚀 Getting Started

### 1. Install Arduino IDE & ESP32 Board Package

1. Download [Arduino IDE](https://www.arduino.cc/en/software) (v1.8+ or v2.x).
2. Open **File → Preferences** and add this URL to *Additional Board Manager URLs*:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Go to **Tools → Board → Board Manager**, search for `esp32` by Espressif and install version **≥ 2.0**.

### 2. Clone or Download This Repo

```bash
git clone https://github.com/your-username/esp32-water-tank.git
cd esp32-water-tank
```

Or download the ZIP from the GitHub **Code** button and extract it.

### 3. Open the Sketch

Open `water_tank_management.ino` in Arduino IDE.

### 4. Configure Your Settings

Edit the **USER CONFIGURATION** block near the top of the file:

```cpp
// ── Tank physical dimensions ──
#define TANK_TOTAL_CM     100.0f   // ← your tank depth in cm
#define SENSOR_OFFSET_CM    5.0f  // ← gap from sensor to full-water level
#define TANK_LENGTH_CM     60.0f  // ← tank length (for litres)
#define TANK_WIDTH_CM      40.0f  // ← tank width  (for litres)

// ── Wi-Fi mode ──
#define WIFI_MODE   WIFI_MODE_AP   // ← AP (hotspot) or WIFI_MODE_STA (home Wi-Fi)

const char* AP_SSID     = "WaterTank_ESP32";
const char* AP_PASSWORD = "watertank123";
```

See the [Configuration](#️-configuration) section for all options.

### 5. Select Board & Port

- **Tools → Board → ESP32 Arduino → ESP32 Dev Module**
- **Tools → Port → COMx** (Windows) or `/dev/ttyUSB0` (Linux/Mac)

### 6. Flash

Click **Upload** (→). Wait for `Done uploading`.

### 7. Connect & Open Dashboard

**AP Mode (default):**
1. On your phone or laptop, join Wi-Fi network `WaterTank_ESP32` (password: `watertank123`).
2. Open a browser and navigate to `http://192.168.4.1`.

**STA Mode:**
1. The ESP32 will connect to your home router.
2. Check the Serial Monitor (115200 baud) for the assigned IP address.
3. Open that IP in your browser from any device on the same network.

---

## ⚙️ Configuration

All configurable values are in one place at the top of `water_tank_management.ino`.

### Pin Assignments

```cpp
#define PIN_TRIG      5    // HC-SR04 Trigger
#define PIN_ECHO     18    // HC-SR04 Echo
#define PIN_RELAY    26    // Relay IN  (active-LOW)
#define PIN_BUZZER   27    // Buzzer +
```

Change these if your wiring is different. Any free GPIO works.

### Tank Dimensions

```cpp
#define TANK_TOTAL_CM     100.0f  // Full depth: sensor face → tank floor (cm)
#define SENSOR_OFFSET_CM    5.0f  // Sensor face → water when tank is full (cm)
#define TANK_LENGTH_CM     60.0f  // For volume — length of rectangular tank (cm)
#define TANK_WIDTH_CM      40.0f  // For volume — width  of rectangular tank (cm)
```

> **Measuring TANK_TOTAL_CM:** Place the sensor at the top, point it straight down, and record the distance reading when the tank is completely empty. That value is your `TANK_TOTAL_CM`.

> **Measuring SENSOR_OFFSET_CM:** Fill the tank to 100% (overflow point) and record the sensor reading. That is your `SENSOR_OFFSET_CM`.

### Auto-Control Thresholds

```cpp
#define PUMP_AUTO_ON_PCT    20   // Pump ON  when level < 20%
#define PUMP_AUTO_OFF_PCT   90   // Pump OFF when level > 90%
#define ALERT_CRITICAL_PCT  10   // Fast buzzer below 10%
#define ALERT_OVERFLOW_PCT  95   // Slow buzzer above 95%
```

### Wi-Fi Modes

| Mode | Constant | Use case |
|---|---|---|
| Access Point | `WIFI_MODE_AP` | No router needed; phone connects directly to ESP32 |
| Station | `WIFI_MODE_STA` | ESP32 joins home Wi-Fi; access from any device on network |

```cpp
// To use home Wi-Fi:
#define WIFI_MODE       WIFI_MODE_STA
const char* STA_SSID     = "YourHomeWiFi";
const char* STA_PASSWORD = "YourPassword";
```

If STA connection fails (wrong password, out of range), the system automatically **falls back to AP mode**.

---

## 🌐 Web Dashboard

Access the dashboard in any modern browser — desktop or mobile.

```
http://192.168.4.1        ← AP mode (default)
http://<assigned-ip>      ← STA mode (check Serial Monitor for IP)
```

### Dashboard Panels

**Tank Visualisation**
An animated SVG tank that fills and empties in real time, with a wave animation on the water surface. The percentage is shown both inside the SVG and as a large number below it.

**Live Stats Cards**
- Sensor distance (cm)
- Water height (cm)
- Volume (litres)
- Pump status (ON / OFF)
- Control mode (AUTO / MANUAL)
- Alert status (OK / LOW / HIGH)

**Controls**
- **Auto / Manual** toggle — switches control mode
- **Pump ON / Pump OFF** — available only in Manual mode
- Progress bar with colour coding (red → amber → blue → green)

**Alert Banners**
- 🔴 Red flashing banner for critical low water
- 🟡 Amber banner for overflow warning

The dashboard polls `/data` every **2 seconds** automatically — no page refresh needed.

---

## 📊 JSON API

The `/data` endpoint returns a JSON object you can use with Home Assistant, Node-RED, or any custom script.

**Request:**
```
GET http://192.168.4.1/data
```

**Response:**
```json
{
  "distanceCm":     45.2,
  "waterHeightCm":  54.8,
  "levelPct":       55,
  "volumeL":        131.52,
  "pumpOn":         false,
  "autoMode":       true,
  "sensorFault":    false,
  "critLow":        false,
  "overflow":       false,
  "uptimeSec":      3720
}
```

**Control endpoints:**

| Endpoint | Method | Body | Action |
|---|---|---|---|
| `/pump` | POST | `state=1` | Pump ON  (manual mode only) |
| `/pump` | POST | `state=0` | Pump OFF (manual mode only) |
| `/mode` | POST | `auto=1`  | Switch to Auto mode |
| `/mode` | POST | `auto=0`  | Switch to Manual mode |

**Example — curl:**
```bash
# Read current state
curl http://192.168.4.1/data

# Switch to manual mode
curl -X POST http://192.168.4.1/mode -d "auto=0"

# Turn pump ON
curl -X POST http://192.168.4.1/pump -d "state=1"
```

**Example — Python:**
```python
import requests

BASE = "http://192.168.4.1"

# Get status
data = requests.get(f"{BASE}/data").json()
print(f"Level: {data['levelPct']}%  |  Pump: {'ON' if data['pumpOn'] else 'OFF'}")

# Switch to auto mode
requests.post(f"{BASE}/mode", data={"auto": "1"})
```

---

## 🖥️ Serial Monitor Commands

Open **Tools → Serial Monitor** at **115200 baud** after flashing.

| Key | Action |
|---|---|
| `p` | Toggle pump ON/OFF *(manual mode only)* |
| `m` | Toggle Auto / Manual mode |
| `s` | Print full status report |
| `?` | Show help |

**Example status output (`s`):**
```
[STATUS]
  Distance    : 44.8 cm
  Water height: 55.2 cm
  Level       : 55%
  Volume      : 132.5 L
  Pump        : OFF
  Mode        : AUTO
  Crit-low    : no
  Overflow    : no
  Uptime      : 3720 s
```

---

## 🔄 Auto vs Manual Mode

```
AUTO MODE
─────────────────────────────────────────────────────
  Level drops below PUMP_AUTO_ON_PCT  (default 20%)
       ↓
  Pump turns ON automatically
       ↓
  Level rises above PUMP_AUTO_OFF_PCT (default 90%)
       ↓
  Pump turns OFF automatically

  Web dashboard pump buttons are DISABLED.


MANUAL MODE
─────────────────────────────────────────────────────
  Automatic thresholds are IGNORED.
  User controls pump via dashboard or serial.
  Buzzer alerts still active.
```

Switching modes via the dashboard takes effect **instantly** — the pump state is preserved when switching from Auto to Manual.

---

## 🔔 Buzzer Alert Logic

| Condition | Behaviour | Default Trigger |
|---|---|---|
| Critical low water | Fast beep (every 400 ms, 2400 Hz) | Level < 10% |
| Overflow warning | Slow beep (every 1100 ms, 1300 Hz) | Level > 95% |
| Pump turned ON | Single short beep (900 Hz, 100 ms) | — |
| Pump turned OFF | Single short beep (450 Hz, 100 ms) | — |
| System boot | Three ascending tones | — |

Alerts are **non-blocking** — the buzzer runs alongside the web server and sensor polling without freezing the system.

---

## 🛠️ Troubleshooting

**Sensor always reads 0% or 100%**
- Double-check TRIG and ECHO wiring.
- Make sure ECHO is voltage-divided to 3.3 V.
- Verify `TANK_TOTAL_CM` matches your actual tank depth.
- Check the Serial Monitor — it prints distance every 500 ms.

**Wi-Fi not showing up**
- Ensure the board flashed successfully (no upload errors).
- Open Serial Monitor at 115200 baud and look for the IP/SSID line.
- In STA mode, confirm your Wi-Fi credentials are correct.

**Pump not switching**
- Confirm your relay is **active-LOW** (most blue relay boards are). If your relay is active-HIGH, change `setPump()` to `digitalWrite(PIN_RELAY, on ? HIGH : LOW)`.
- Check relay LED — it should light when pump is commanded ON.
- Test relay independently with a simple `digitalWrite` sketch.

**Dashboard not loading**
- Confirm you are connected to `WaterTank_ESP32` in AP mode.
- Try `http://192.168.4.1` manually (some browsers auto-redirect to HTTPS).
- Disable mobile data on your phone when using AP mode.

**Readings are noisy/unstable**
- The code already averages 5 samples. If still noisy, increase `SENSOR_SAMPLES` to 7–10.
- Ensure there are no obstructions (pipes, floats) in the ultrasonic beam path.
- Mount the sensor so it is perfectly vertical.

---

## 📁 Project Structure

```
esp32-water-tank/
│
├── water_tank_management.ino   # Main sketch (all code in one file)
├── README.md                   # This file
└── LICENSE                     # MIT License
```

Everything is in a single `.ino` file for simplicity — no extra headers or source files needed.

---

## 🗺️ Possible Future Improvements

- [ ] EEPROM / Preferences to save thresholds across reboots
- [ ] OTA (Over-The-Air) firmware update support
- [ ] OLED display for local readout (no phone needed)
- [ ] Telegram / WhatsApp bot alerts via HTTP
- [ ] Data logging to SD card or Google Sheets
- [ ] Multiple tank support
- [ ] Flow sensor integration for leak detection
- [ ] Circular/cylindrical tank volume formula option

PRs and issues welcome!

---

## 🤝 Contributing

1. Fork the repository.
2. Create a feature branch: `git checkout -b feature/your-feature`.
3. Commit your changes: `git commit -m "Add: your feature description"`.
4. Push to your fork: `git push origin feature/your-feature`.
5. Open a Pull Request.

Please keep the single-file structure and the **USER CONFIGURATION** block at the top for ease of use.

---

## 📄 License

This project is released under the [MIT License](LICENSE).

You are free to use, modify, and distribute this code for personal and commercial projects. Attribution appreciated but not required.

---

<div align="center">

Made with ❤️ using ESP32 + Arduino

**⭐ Star this repo if it helped your project!**

</div>
