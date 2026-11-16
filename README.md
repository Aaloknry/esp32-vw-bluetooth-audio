# 🎵 Virtual USB Bluetooth Audio Bridge (VW-Audio-Bridge)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: ESP32 / ESP32-S3](https://img.shields.io/badge/Platform-ESP32%20%7C%20ESP32--S3-red.svg)](https://www.espressif.com/)
[![Framework: Arduino / PlatformIO](https://img.shields.io/badge/Framework-PlatformIO%20Arduino-blue.svg)](https://platformio.org/)
[![Status: Production Ready](https://img.shields.io/badge/Status-Tested%20%26%20Working-success.svg)]()

> **Transform ANY legacy USB-only car stereo (Volkswagen RCD 310, RCD 510, RCD 300, MDI / Media-In) or home audio player into a modern, wireless Bluetooth A2DP audio receiver with sub-second latency and zero track expiration.**

---

## 🌟 The Problem & The Innovation

Millions of modern-classic vehicles (such as the Volkswagen Vento, Polo, Golf, Jetta, Passat, Skoda Rapid, Laura, Octavia, SEAT, Ford, Honda, and Toyota) come equipped with factory head units featuring a **USB flash drive port**, but **no Bluetooth audio streaming (A2DP)** (or Bluetooth limited strictly to phone calls).

### Existing Workarounds and Why They Fail:
* ❌ **FM Transmitters**: Poor sound quality, background hiss, signal drops, frequency clutter.
* ❌ **Aux Cables**: Messy wires across the cabin, no dashboard integration, prone to ground-loop alternator whine.
* ❌ **Cheap USB Bluetooth Dongles**: These try to emulate a flash drive by recording audio into a dummy file. In practice, they freeze, crash the stereo with `"USB Device Error"`, introduce 5–15 seconds of audio lag, or stop playing after 3 minutes.

### The Solution:
**VW-Audio-Bridge** presents a virtual **2.0 GB FAT16 USB flash drive (`VW_AUDIO`)** containing an MP3 track called `VW_MUSIC.MP3`.
When your car stereo reads this file, the microcontroller serves a **live, real-time MP3 stream** encoded directly from your smartphone's Bluetooth audio with **< 0.5s latency**.

When you disconnect or pause Spotify, the system smoothly loops a pleasant **ambient standby chime**, keeping the car stereo awake and ready. As soon as you hit play, live music instantly takes over!

---

## 🚀 Key Features

* ⚡ **True Sub-Second Latency**: Audio streams from Spotify / Apple Music / YouTube into your car speakers with under ~0.3s delay.
* 🚗 **Resync-On-Resume (Ignition-Cycle Proof)**: When you turn off the car for fuel, groceries, or a break, the factory radio saves its byte playback position. When restarted, the system detects the resumption and immediately resynchronizes to the **current live audio stream** instead of playing stale buffered audio from hours ago.
* 🔄 **Infinite 37.2-Hour Continuous Streaming**: Implements a 2.0 GB FAT16 virtual volume with exactly 65,500 clusters ($65,500 < 65,525$ strictly complies with Microsoft FAT16 specifications, preventing corrupt FAT32 fallback on automotive head units).
* 🔔 **Pristine Standby Comfort Chime**: Continuous 128 kbps 44.1 kHz stereo ambient chime loop prevents `"USB Error"` or `"No Media"` popups when your phone is disconnected.
* 🛡️ **Zero-Chime Music Lock**: Once your phone starts streaming, chime audio is completely locked out—no accidental chime bursts during playback.
* 💡 **Onboard RGB Status Diagnostics**:
  * 🔵 **BLUE LED**: Standby Chime mode (waiting for Bluetooth connection).
  * 🟢 **BRIGHT GREEN LED**: Live Bluetooth audio streaming actively encoded and fed to USB.
* 🔌 **100% Plug-and-Play**: Foolproof operation. No buttons to press, no screen to configure. Pairs automatically with your phone whenever the car ignition is turned on.

---

## 📱 Compatibility

### Automotive Head Units:
* **Volkswagen**: RCD 310, RCD 510, RNS 315, RCD 300, MDI (Media Device Interface / AMI cable).
* **Skoda**: Swing, Bolero, Amundsen.
* **SEAT**: Media System 2.0 / 2.1.
* **Universal**: Any car head unit (Ford SYNC, Honda, Toyota, Hyundai, Alpine, Kenwood, Pioneer, Sony) that supports USB MP3 playback.

### Non-Automotive Applications:
* Home Hi-Fi receivers and amplifiers with front USB ports.
* Vintage boomboxes and party speakers with USB ports.
* Marine / boat audio systems with USB media inputs.

---

## 🛠️ Hardware Requirements & Bill of Materials (BOM)

| Component | Role | Description |
| :--- | :--- | :--- |
| **ESP32-S3 DevKitC-1 (N16R8)** | **USB & DSP Engine** | Dual-Core LX7 @ 240 MHz, 8MB Octal PSRAM, Native USB OTG. Runs TinyUSB MSC and Shine MP3 encoder. |
| **ESP32-WROOM-32** | **Bluetooth Classic Receiver** | Dedicated to ESP-IDF Bluetooth Classic A2DP Sink (`VW-Vento-Audio`). |
| **4x Jumper Wires** | Interconnect | 5V, GND, TX, RX high-speed link. |
| **Dual USB Car Charger** | Power | Powers both boards from the 12V cigarette lighter / accessory socket. |

---

## 📐 Wiring Diagram

Connect the two ESP boards using 4 jumper wires:

```
+------------------------+                    +------------------------+
|    ESP32-WROOM-32      |                    |     ESP32-S3-N16R8     |
| (Bluetooth A2DP Sink)  |                    | (USB MSC & MP3 Engine) |
|                        |                    |                        |
|       TX0 (GPIO 1) ----+------------------->+-- RX1 (GPIO 18)        |
|       RX0 (GPIO 3) <---+--------------------+-- TX1 (GPIO 17)        |
|                GND ----+--------------------+-- GND                  |
|                 5V ----+--------------------+-- 5V                   |
+------------------------+                    +-----------+------------+
                                                          |
                                                     [LEFT USB-C]
                                                          |
                                                          v
                                                 To Car Stereo USB Port
```

> **Note on Power**: Connect the Left USB-C port of the ESP32-S3 to the Car Stereo USB port. To ensure rock-solid stability during Bluetooth search, supply 5V to the WROOM board via its USB port or the car's 12V USB adapter.

---

## 💻 Repository Structure

```
.
├── s3_bridge/            # Primary ESP32-S3 firmware (USB MSC, FAT16 engine, Shine MP3 encoder)
├── wroom_firmware/       # Dedicated ESP32-WROOM Bluetooth A2DP Sink receiver (2 Mbps UART stream)
├── s3_flasher/           # Transparent UART flasher utility (flashes WROOM via S3 if WROOM USB is broken)
├── assets/               # Audio assets (ambient chime, silence frames)
├── ABOUT.md              # In-depth technical architecture and engineering deep-dive
├── LICENSE               # MIT License
└── README.md             # This file
```

---

## ⚡ Quick Start & Flashing Guide

### 1. Prerequisites
* Install [VS Code](https://code.visualstudio.com/) with the [PlatformIO IDE](https://platformio.org/) extension.
* Clone this repository:
  ```bash
  git clone https://github.com/Aaloknry/esp32-vw-bluetooth-audio.git
  cd esp32-vw-bluetooth-audio
  ```

### 2. Flash the ESP32-WROOM (Bluetooth Receiver)
1. Plug the ESP32-WROOM into your PC via USB.
2. In PlatformIO, open the `wroom_firmware` project.
3. Build and upload:
   ```bash
   pio run -d wroom_firmware -t upload
   ```
*(If your WROOM has a faulty onboard USB chip, flash it transparently through the S3 using `s3_flasher`)*.

### 3. Flash the ESP32-S3 (USB Bridge & MP3 Engine)
1. Plug the ESP32-S3 into your PC using the **Right USB-C port** (UART / COM port).
2. Open the `s3_bridge` project in PlatformIO.
3. Build and upload:
   ```bash
   pio run -d s3_bridge -t upload
   ```

---

## 🚗 Car Setup & Operation

1. Connect the 4 jumper wires between WROOM and S3 as shown in the wiring diagram.
2. In your car, plug the **Left USB-C port** of the ESP32-S3 into your car's USB port (or VW MDI cable).
3. Turn on the car ignition:
   * The car stereo display will read `"USB Loading..."` and then mount `VW_AUDIO`.
   * The track `VW_MUSIC.MP3` will start playing.
   * You will hear the gentle ambient chime through your car speakers.
   * The onboard LED will glow **BLUE**.
4. On your smartphone:
   * Open Bluetooth settings and connect to **`VW-Vento-Audio`**.
   * Play any song on Spotify, YouTube, Apple Music, or Podcasts.
   * The onboard LED immediately switches to **BRIGHT GREEN** and your music plays crystal-clear through the car speakers!
5. **Turn off the engine for a break?**
   * When you return and start the car, the stereo resumes instantly on the live stream without lag or rewinding!

---

## 📄 License

This project is licensed under the **MIT License** — feel free to use, modify, distribute, or integrate it into commercial and private projects. See [LICENSE](LICENSE) for details.

Developed with ❤️ by **Aalok Narayan ([@Aaloknry](https://github.com/Aaloknry))**.
