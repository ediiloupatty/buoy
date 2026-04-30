<div align="center">
  # Smart Buoy IoT System 🌊
  **Enterprise-Grade Autonomous Aquaculture Monitoring Platform**
  
  [![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](#)
  [![Firmware](https://img.shields.io/badge/firmware-v1.2.0-blue.svg)](#)
  [![Platform](https://img.shields.io/badge/platform-ESP32-lightgrey.svg)](#)
  [![Database](https://img.shields.io/badge/database-Firebase_RTDB-FFCA28.svg)](#)
  [![Client](https://img.shields.io/badge/client-Flutter-02569B.svg)](#)

  <br />
  <img src="img/Diagram.png" alt="Smart Buoy Architecture Diagram" width="100%">
</div>

---

## 📖 Executive Summary
The **Smart Buoy IoT System** is a robust, autonomous aquatic monitoring solution specifically engineered for commercial aquaculture (shrimp/fish farming). By leveraging real-time telemetry and highly optimized NoSQL data streams, it provides farm operators with instantaneous visibility into critical water quality parameters, significantly reducing mortality rates and optimizing harvest yields.

## ✨ Key Capabilities
- **Continuous Telemetry:** 24/7 autonomous monitoring of **Temperature**, **pH**, and **Turbidity**.
- **Bandwidth-Optimized Protocol:** Implements intelligent threshold-based transmission to reduce payload overhead by up to 80%, crucial for low-bandwidth cellular environments (e.g., SIM800L).
- **Redundant Power System:** Designed for sustainable deployment utilizing internal Li-Ion batteries coupled with top-mounted solar arrays.
- **Enterprise Data Architecture:** Fully integrated with Firebase Realtime Database utilizing distinct data pipelines for real-time dashboarding and historical logging.

---

## 🏗️ System Architecture

### Telemetry Pipeline
The firmware is designed to aggressively minimize MCU workloads and network payloads. Data is dispatched exclusively to **Firebase Realtime Database** via two discrete logical paths:

```text
[ Smart Buoy (ESP32 Node) ]
    │
    ├──→ /smart_buoy/live/      [ Real-Time Stream ]
    │                             ▸ Trigger: Threshold Delta (temp>0.1, pH>0.05, turb>5)
    │                             ▸ Target: Instantaneous mobile dashboard synchronization
    │
    └──→ /smart_buoy/history/   [ Time-Series Data Lake ]
                                  ▸ Trigger: 10-minute Cron Interval
                                  ▸ Target: Historical trend analysis & charting
```

### Payload Specifications
To ensure ultra-lightweight M2M (Machine-to-Machine) communication, string formatting is deferred to the client application.

**1. Live Node (`/smart_buoy/live/`)**
| Key | Type | Description |
|---|---|---|
| `temp` | `double` | Water Temperature (°C) |
| `pH` | `double` | Acidity Level (pH) |
| `turb` | `int` | Raw Turbidity ADC Value |

**2. History Node (`/smart_buoy/history/`)**
| Key | Type | Description |
|---|---|---|
| `temp` | `double` | Water Temperature (°C) |
| `pH` | `double` | Acidity Level (pH) |
| `turb` | `int` | Raw Turbidity ADC Value |
| `ts` | `int` | Unix Epoch Timestamp |

> **Note:** Qualitative status strings (e.g., "Clear", "Dirty") are omitted from the payload to conserve bandwidth. Client applications must compute states dynamically.

---

## 🛠️ Hardware Requirements
- **MCU:** DOIT ESP32 DevKit V1
- **Sensors:** Analog pH Sensor, DS18B20 Waterproof Temperature Sensor, Analog Turbidity Sensor
- **Power:** TP4056 Charge Module, 18650 Li-Ion Battery, 5V Solar Panel
- *(Future Roadmap)* **Connectivity:** SIM800L v2 GSM Module

---

## 💻 Firmware Installation & Configuration

### Prerequisites
- **Arduino IDE 2.x** or PlatformIO.
- **ESP32 Core** installed via Board Manager.
- Required Libraries:
  - `Firebase ESP32 Client` by Mobizt
  - `DallasTemperature` by Miles Burton
  - `OneWire` by Paul Stoffregen

### Build Instructions
1. **Clone the Repository:**
   ```bash
   git clone https://github.com/your-org/smart-buoy-iot.git
   ```
2. **Environment Configuration:**
   Navigate to `Config.h` and provision your secure credentials:
   ```c
   // Network Configuration
   static const char *ssid = "YOUR_WIFI_SSID";
   static const char *password = "YOUR_WIFI_PASSWORD";

   // Firebase Configuration
   #define FIREBASE_HOST "your-project-id-default-rtdb.region.firebasedatabase.app"
   #define FIREBASE_AUTH "your_database_secret"
   ```
3. **Compile & Flash:** Select **DOIT ESP32 DEVKIT V1** and execute the upload sequence. Hold the `BOOT` button if the terminal halts at `Connecting...`.

---

## 📱 Mobile Client Integration (Flutter)

The accompanying mobile application acts as the operations center.

### 1. Initialization
Bind the client to Firebase using the FlutterFire CLI:
```bash
flutterfire configure --project=your-firebase-project-id
flutter pub add firebase_core firebase_database
```

### 2. Client-Side Data Handling
To adhere to the lightweight payload architecture, the client is responsible for data transformation:

```dart
import 'package:firebase_database/firebase_database.dart';

class WaterQualityService {
  final DatabaseReference _liveRef = FirebaseDatabase.instance.ref('smart_buoy/live');

  // Business Logic: Compute qualitative state from raw telemetry
  String computeTurbidityState(int rawTurbidity) {
    if (rawTurbidity < 1500) return 'Jernih (Clear)';
    if (rawTurbidity < 3000) return 'Keruh (Cloudy)';
    return 'Kotor (Critical)';
  }

  // Real-time telemetry subscription
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

> **Security Advisory:** Never commit `.env`, `google-services.json`, or `GoogleService-Info.plist` to version control. Strict IAM rules must be enforced on the Firebase console for production deployments.

---
<div align="center">
  <p>© 2026 Smart Buoy Systems. All Rights Reserved.</p>
</div>
