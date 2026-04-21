# ESP32 Voice-Activated Target Thrower System

A wireless, voice-activated clay pigeon trap thrower controller built with two ESP32-S3 boards communicating via ESP-NOW, plus a corded remote handset for manual control. Yell "pull" (or just make a loud enough noise), and the trap fires — no buttons, no wires between shooter and trap.

---

## Overview

This system uses three units:

- **Mic Unit (ESP32-S3 #1)** — Sits near the shooter. Listens for audio above a configurable threshold using an INMP441 I2S microphone. When triggered, sends a fire command wirelessly to the controller unit via ESP-NOW.
- **Controller Unit (ESP32-S3 #2)** — Mounts at the trap machine. Receives ESP-NOW triggers from the mic unit, accepts manual fire and control signals from the corded remote, and drives the KY-019 relay to fire the trap.
- **Corded Remote Box** — 3D-printed handheld box on a 5-conductor cable (6–10 meters). Contains the master power switch (SW1), voice disable switch (SW2), and manual fire button (BTN).

The original approach used ML-based keyword spotting, but threshold-based audio detection proved more reliable in outdoor/noisy environments and removed the need for a trained model.

---

## Hardware Required

### Mic Unit (ESP32-S3 #1)
| Component | Notes |
|---|---|
| ESP32-S3 N16R8 | 16MB flash, 8MB PSRAM |
| INMP441 I2S MEMS Microphone | Omnidirectional, I2S digital output |
| SPST toggle or rocker switch | Panel-mount, 12mm or smaller cutout |
| 12mm LED pilot light, 3V rated | Search "12mm LED indicator pilot light 3V" — built-in resistor |
| 5V portable phone charger | Powers unit via USB-C |

### Controller Unit (ESP32-S3 #2)
| Component | Notes |
|---|---|
| ESP32-S3 N16R8 | 16MB flash, 8MB PSRAM |
| KY-019 5V Relay Module | Active-HIGH trigger (GPIO 10) |
| 10k ohm resistors x2 | Pullups for GPIO 4 (SW2) and GPIO 9 (BTN) |
| 5V power supply | Powers via USB-C |

### Corded Remote Box
| Component | Notes |
|---|---|
| SW1 — SPST panel-mount toggle | Master power switch — rated 5V DC minimum |
| SW2 — SPST panel-mount toggle | Voice disable switch |
| BTN — Momentary NO pushbutton | Manual fire — red color recommended |
| 3D-printed enclosure | Hammond 1591B or similar ABS as alternative |
| 5-conductor cable, 6–10m | Security/alarm cable or Cat5e (5 wires used) |
| GX16-5 aviation connector | Optional but recommended for clean detachable connection |
| PG7 cable gland | Strain relief at box entry point |

---

## Wiring

See [`wiring_diagram.svg`](./wiring_diagram.svg) for full pin-level diagrams for all three units.

### Mic Unit — INMP441 to ESP32-S3 #1

> ⚠️ **INMP441 VDD must connect to 3.3V only. Connecting to 5V will permanently damage the microphone.**

| INMP441 Pin | Wire Color | ESP32-S3 #1 GPIO | Notes |
|---|---|---|---|
| VDD | Red | 3.3V | 3.3V only |
| GND | Black | GND | Common ground |
| SD (Data) | Yellow | GPIO 8 | I2S serial data |
| SCK (Clock) | Blue | GPIO 5 | I2S bit clock |
| WS (Word Select) | Green | GPIO 6 | I2S word select / LR clock |
| L/R | Black | GND | Tie to GND = left channel |

### Mic Unit — Power Switch + LED

| Component | Wire Color | Connection | Notes |
|---|---|---|---|
| SW — COM | Red | From 5V supply | In series with 5V to USB-C |
| SW — NO | Red | To ESP32-S3 #1 USB/VIN | Output of switch to board |
| LED — Anode (+) | Orange | 3.3V | On when board is running |
| LED — Cathode (−) | Black | GND | Ground return |

### Controller Unit — KY-019 Relay to ESP32-S3 #2

| Relay Pin | Wire Color | ESP32-S3 #2 Pin | Notes |
|---|---|---|---|
| VCC | Red | 5V / VIN | 5V rail — not 3.3V |
| GND | Black | GND | Common ground |
| IN (Signal) | Orange | GPIO 10 | Active HIGH — KY-019 |

### Controller Unit — Relay Output to Trap

| Relay Terminal | Wire Color | Connects To | Notes |
|---|---|---|---|
| COM | White | Safety switch → Trap Wire 1 | Common terminal |
| NO | White | Trap Wire 2 | Closes for 300ms on trigger |
| NC | — | Not connected | Leave unconnected |

### Corded Remote — 5-Conductor Cable

| Pin | Wire Color | Carries | Notes |
|---|---|---|---|
| 1 | Red | SW1 input — from 5V supply | 5V feeds into SW1 COM inside remote box |
| 2 | White | SW1 output — to controller VIN | SW1 NO feeds controller VIN |
| 3 | Green | SW2 — voice disable | GPIO 4; 10k pullup to 3.3V at controller |
| 4 | Yellow | BTN — manual fire | GPIO 9; 10k pullup to 3.3V at controller |
| 5 | Black | GND | Common ground return |

---

## Software Setup

### Prerequisites
- [Arduino IDE 2.x](https://www.arduino.cc/en/software) or PlatformIO
- [arduino-esp32 board package](https://github.com/espressif/arduino-esp32) v2.0.14 or later
- No additional libraries required (ESP-NOW and I2S are included in the ESP32 Arduino core)

### Flash Configuration (ESP32-S3 N16R8)
When uploading, set the following in Arduino IDE under **Tools**:

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB |
| PSRAM | OPI PSRAM |
| Partition Scheme | Default 4MB with spiffs (or custom) |
| USB CDC On Boot | Enabled (for Serial monitor) |

> **Important:** The N16R8 variant requires **OPI PSRAM** selected — not "Quad" or "Disabled". Wrong PSRAM config will cause boot loops.

### Getting the Code

```bash
git clone https://github.com/YOUR_USERNAME/ESP32-Voice-Activated-Target-Thrower-System.git
cd ESP32-Voice-Activated-Target-Thrower-System
```

Open `mic_unit/mic_unit.ino` and `controller_unit/controller_unit.ino` in Arduino IDE.

---

## Configuration

### Mic Unit — `mic_unit.ino`

```cpp
// MAC address of your controller ESP32-S3
// Find it by running controller firmware and checking Serial output
uint8_t controllerMAC[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

// Trigger parameters — adjust at the range based on real conditions
#define AMPLITUDE_THRESHOLD   2000    // Raise if wind/noise causes false triggers
#define MIN_DURATION_MS       30      // Lower if short calls are missed
#define MAX_DURATION_MS       1500    // Raise if long calls are cut off
#define COOLDOWN_MS           2000    // Raise if double triggers occur
```

### Controller Unit — `controller_unit.ino`

```cpp
#define RELAY_PIN       10    // GPIO 10 — KY-019 relay signal (active HIGH)
#define VOICE_DISABLE   4     // GPIO 4  — SW2 from remote (LOW = voice ignored)
#define MANUAL_FIRE     9     // GPIO 9  — BTN from remote (LOW = fire)
#define RELAY_HOLD_MS   300   // Duration relay stays closed (milliseconds)
```

---

## How It Works

1. The mic unit continuously samples audio from the INMP441 via I2S.
2. When peak amplitude exceeds `AMPLITUDE_THRESHOLD` for between `MIN_DURATION_MS` and `MAX_DURATION_MS`, a fire command is sent via ESP-NOW to the controller's MAC address.
3. A `COOLDOWN_MS` timer prevents re-triggering until the cooldown expires.
4. The controller receives the ESP-NOW packet. If SW2 (voice disable) is OFF, it pulls GPIO 10 HIGH for `RELAY_HOLD_MS`, closing the relay and completing the trap's button circuit.
5. The manual fire button (BTN) on the remote pulls GPIO 9 LOW and triggers the relay directly, regardless of SW2 state.
6. SW1 on the remote sits in series with the 5V supply — flipping it off cuts all power to the controller unit.

### Remote Switch Behavior

| SW1 | SW2 | BTN | Result |
|---|---|---|---|
| OFF | — | — | System fully off — no power to controller |
| ON | OFF (voice on) | Not pressed | Normal operation — voice detection active |
| ON | OFF (voice on) | Pressed | Manual fire |
| ON | ON (voice off) | Not pressed | Voice disabled — standby |
| ON | ON (voice off) | Pressed | Manual fire only |

---

## Finding Your Controller's MAC Address

Flash the controller first, then open Serial Monitor at 115200 baud. The MAC address is printed on boot:

```
Controller ready.
MAC Address: 9C:13:9E:AB:BE:58
```

Copy this into `controllerMAC[]` in the mic unit sketch before flashing.

---

## Flashing Order

| Step | Action |
|---|---|
| 1 | Flash MAC-reader sketch to ESP32-S3 #2 |
| 2 | Note MAC address from Serial Monitor |
| 3 | Paste MAC into `controllerMAC[]` in mic unit firmware |
| 4 | Flash controller firmware to ESP32-S3 #2 |
| 5 | Flash mic unit firmware to ESP32-S3 #1 |
| 6 | Power both units — Serial Monitor confirms ready state |

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| Trap fires constantly / too easily | Threshold too low | Increase `AMPLITUDE_THRESHOLD` |
| Trap doesn't respond to voice | Threshold too high, or mic wiring | Lower threshold; verify INMP441 wiring and L/R tied to GND |
| Boot loop on either unit | Wrong PSRAM setting | Set PSRAM to **OPI PSRAM** in Arduino IDE |
| ESP-NOW send fails | Wrong MAC address | Re-check `controllerMAC[]` against controller Serial output |
| Relay clicks but trap doesn't fire | Wrong relay terminals | Ensure COM and NO are used, not COM and NC |
| No audio data / all zeros | I2S pin mismatch | Verify SD=GPIO 8, SCK=GPIO 5, WS=GPIO 6 |
| BTN fires but voice doesn't | SW2 (voice disable) is ON | Flip SW2 off to re-enable voice |
| System dead with SW1 on | Cable connection issue | Check 5-conductor cable pin 1 (Red) and pin 2 (White) |
| Relay fires on its own | BTN pullup missing | Verify 10k pullup on GPIO 9 to 3.3V |

---

## Project Structure

```
ESP32-Voice-Activated-Target-Thrower-System/
├── mic_unit/
│   └── mic_unit.ino            # Mic unit firmware (ESP32-S3 #1)
├── controller_unit/
│   └── controller_unit.ino     # Controller unit firmware (ESP32-S3 #2)
├── wiring_diagram.svg           # Full wiring reference — all three units
├── LICENSE
└── README.md
```

---

## License

This project is licensed under the **GNU General Public License v3.0**. See [`LICENSE`](./LICENSE) for details.

In short: you're free to use, modify, and distribute this project, but any derivative works must also be released under GPL v3.

---

## About

Built by [Broken Target Machine & Gunsmithing](https://github.com/YOUR_USERNAME) as a practical tool for the shooting range. Born out of the desire to shoot whenever I wanted and not need a second person.
