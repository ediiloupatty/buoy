<div align="center">
  <h1>Smart Buoy IoT System 🌊</h1>
  <h3>Autonomous Aquaculture Monitoring Platform</h3>
  
  [![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](#)
  [![Firmware](https://img.shields.io/badge/firmware-v2.0.0-blue.svg)](#)
  [![Platform](https://img.shields.io/badge/platform-ESP32-lightgrey.svg)](#)
  [![Database](https://img.shields.io/badge/database-Firebase_RTDB-FFCA28.svg)](#)
  [![Client](https://img.shields.io/badge/client-Flutter-02569B.svg)](#)
  [![Connectivity](https://img.shields.io/badge/connectivity-SIM800L_GPRS-E91E63.svg)](#)

  <br />
  <br />
  <img src="img/Diagram.png" alt="Smart Buoy Architecture Diagram" width="100%">
</div>

---

## 📖 Executive Summary
The **Smart Buoy IoT System** is a robust, autonomous aquatic monitoring solution specifically engineered for commercial aquaculture (shrimp and fish farming). By leveraging real-time telemetry over cellular GPRS networks and an ultra-low-power deep sleep architecture, it provides farm operators with instantaneous visibility into critical water quality parameters while maintaining indefinite solar-powered operation.

## ✨ Key Capabilities
- **Continuous Telemetry:** 24/7 autonomous monitoring of **Temperature**, **pH**, and **Turbidity**.
- **Cellular Connectivity:** SIM800L GPRS modem enables deployment in remote aquaculture sites without WiFi infrastructure.
- **Deep Sleep Architecture:** ESP32 enters deep sleep between readings (~10µA), waking every 2 minutes for sensor polling and data transmission.
- **Solar-Sustainable:** Optimized power budget ensures indefinite operation with a 12V/2W solar panel and 6700mAh battery bank.
- **Bandwidth-Optimized Protocol:** Implements intelligent threshold-based transmission to reduce payload overhead by up to 80%.
- **Enterprise Data Architecture:** Fully integrated with Firebase Realtime Database utilizing distinct data pipelines for real-time dashboarding and historical logging.

---

## 🏗️ System Architecture

### Telemetry Pipeline
The firmware implements an aggressive power-optimized duty cycle. Data is dispatched exclusively to **Firebase Realtime Database** via the SIM800L GPRS modem using the **Firebase REST API** over HTTPS:

```text
[ Smart Buoy (ESP32 Node) ]
    │
    │  ← Deep Sleep (2 min) → Wake → Read Sensors → Transmit → Sleep
    │
    ├──→ /smart_buoy/live/      [ Real-Time Stream ]
    │                             ▸ Method: HTTPS PUT (overwrite)
    │                             ▸ Trigger: Threshold Delta (temp>0.1, pH>0.05, turb>5)
    │                             ▸ Target: Instantaneous mobile dashboard synchronization
    │
    └──→ /smart_buoy/history/   [ Time-Series Data Lake ]
                                  ▸ Method: HTTPS POST (append)
                                  ▸ Trigger: Every 5th boot (~10 minutes)
                                  ▸ Target: Historical trend analysis & charting
```

### Deep Sleep Execution Model
```text
┌──────────┐     ┌─────────────┐     ┌──────────┐     ┌──────────┐
│  WAKE UP │────→│ READ SENSORS│────→│ SEND DATA│────→│DEEP SLEEP│
│  (boot)  │     │ pH,Temp,Turb│     │ via GPRS │     │ 2 minutes│
└──────────┘     └─────────────┘     └──────────┘     └─────┬────┘
     ▲                                                       │
     └───────────────── RTC Timer Wakeup ────────────────────┘
```

### Payload Specifications
To ensure ultra-lightweight M2M (Machine-to-Machine) communication, string formatting is deferred to the client application.

**1. Live Node (`/smart_buoy/live/`)**
| Key | Type | Description |
|:---:|:---:|---|
| `temp` | `double` | Water Temperature (°C) |
| `pH` | `double` | Acidity Level (pH) |
| `turb` | `int` | Raw Turbidity ADC Value |

**2. History Node (`/smart_buoy/history/`)**
| Key | Type | Description |
|:---:|:---:|---|
| `temp` | `double` | Water Temperature (°C) |
| `pH` | `double` | Acidity Level (pH) |
| `turb` | `int` | Raw Turbidity ADC Value |
| `ts` | `int` | Unix Epoch Timestamp |

> **Architecture Note:** Qualitative status strings (e.g., "Clear", "Dirty") and date/time formatting are intentionally omitted from the payload to conserve bandwidth. Client applications must compute and translate these states dynamically.

---

## 🛠️ Hardware Requirements
- **Core MCU:** DOIT ESP32 DevKit V1
- **Sensors:** Analog pH Sensor, DS18B20 Waterproof Temperature Sensor, Analog Turbidity Sensor
- **Connectivity:** SIM800L v2.0 Quad Band GSM/GPRS Module + External Antenna
- **Power Module:** 12V/2W Solar Panel, CN3791 MPPT Solar Charger, BMS 1S, 2× 18650 Li-Ion Battery (6700mAh), MT3608 Boost Converter (ESP32), XL6009 Boost Converter (SIM800L), 1000µF Capacitor

---

## 💻 Firmware Installation Guide

### System Prerequisites
- **Arduino IDE 2.x** or PlatformIO.
- **ESP32 Core** installed via Board Manager.
- Required Libraries:
  - `DallasTemperature` (By Miles Burton)
  - `OneWire` (By Paul Stoffregen)

### Compilation Steps
1. **Clone the Repository:**
   ```bash
   git clone https://github.com/ediiloupatty/buoy.git
   ```
2. **Environment Configuration:**
   Open the `Config.h` file and provision your secure credentials:
   ```c
   // SIM800L UART Pins
   #define SIM_RX   16   // ESP32 RX ← SIM800L TX
   #define SIM_TX   17   // ESP32 TX → SIM800L RX
   
   // APN Configuration (adjust for your provider)
   static const char *APN = "internet";

   // Firebase Configuration
   #define FIREBASE_HOST "your-project-id.region.firebasedatabase.app"
   #define FIREBASE_AUTH "your_database_secret_key"
   ```
3. **Compile & Flash:** Select the **DOIT ESP32 DEVKIT V1** board and execute the upload sequence. Hold the physical `BOOT` button on the ESP32 if the terminal hangs at the `Connecting...` prompt.

### pH Calibration
The firmware supports interactive pH calibration at boot time. See [KALIBRASI.md](KALIBRASI.md) for the full calibration guide.

At startup, the firmware waits **5 seconds** for serial input. Type any calibration command (`CAL4`, `CAL7`, `CALSAVE`, `CALINFO`) to enter calibration mode. Type `EXIT` to return to normal operation.

---

## 📱 Mobile Client Integration (Flutter)

The companion mobile application serves as the operations command center.

### 1. Client Initialization
Bind the client application to Firebase using the FlutterFire CLI:
```bash
flutterfire configure --project=your-firebase-project-id
flutter pub add firebase_core firebase_database
```

### 2. Client-Side Data Handling
To adhere to the lightweight payload architecture, the client is entirely responsible for data transformation and smoothing:

```dart
import 'package:firebase_database/firebase_database.dart';

class WaterQualityService {
  final DatabaseReference _liveRef = FirebaseDatabase.instance.ref('smart_buoy/live');

  // Business Logic: Compute qualitative status from raw telemetry
  String computeTurbidityState(int rawTurbidity) {
    if (rawTurbidity < 1500) return 'Clear';
    if (rawTurbidity < 3000) return 'Cloudy';
    return 'Critical / Dirty';
  }

  // Subscribe to real-time telemetry updates
  void subscribeToTelemetry() {
    _liveRef.onValue.listen((event) {
      if (event.snapshot.value == null) return;
      
      final payload = event.snapshot.value as Map<dynamic, dynamic>;
      final double temp = (payload['temp'] ?? 0).toDouble();
      final double ph = (payload['pH'] ?? 0).toDouble();
      final int turb = (payload['turb'] ?? 0).toInt();
      
      final String status = computeTurbidityState(turb);
      
      print('Telemetry Update -> Temp: $temp°C | pH: $ph | Status: $status');
    });
  }
}
```

---
<div align="center">
  <p>© 2026 Smart Buoy Systems. All Rights Reserved.</p>
</div>
