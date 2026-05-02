# 📘 Arsitektur Sistem — Smart Buoy IoT System

Dokumen ini menjelaskan arsitektur lengkap sistem Smart Buoy IoT, mencakup subsistem perangkat keras, catu daya, firmware, dan aliran data telemetri.

---

## 1. Gambaran Umum Sistem

**Smart Buoy IoT System** adalah platform pemantauan kualitas air otonom yang dirancang khusus untuk lingkungan akuakultur komersial (tambak udang dan ikan). Sistem ini mengukur tiga parameter kualitas air secara real-time dan mengirimkan data telemetri ke cloud untuk diakses melalui aplikasi mobile dan web dashboard.

### Parameter yang Dipantau

| No | Parameter | Sensor | Satuan | Fungsi |
|----|-----------|--------|--------|--------|
| 1 | **Suhu Air** | DS18B20 (Waterproof) | °C | Mendeteksi perubahan suhu yang dapat mempengaruhi kesehatan biota |
| 2 | **Tingkat pH** | Sensor pH Analog | pH (0–14) | Mengukur keasaman/kebasaan air tambak |
| 3 | **Kekeruhan** | Sensor Turbidity Analog | ADC (0–4095) | Mengukur kejernihan air |

---

## 2. Diagram Blok Sistem

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        SMART BUOY (FIELD UNIT)                         │
│                                                                         │
│  ┌─────────────┐   ┌──────────────┐   ┌──────────────┐                 │
│  │  DS18B20    │   │  pH Sensor   │   │  Turbidity   │                 │
│  │  (GPIO 4)   │   │  (GPIO 34)   │   │  (GPIO 35)   │                 │
│  └──────┬──────┘   └──────┬───────┘   └──────┬───────┘                 │
│         │ OneWire         │ Analog           │ Analog                   │
│         └────────────┬────┴──────────────────┘                         │
│                      ▼                                                  │
│              ┌──────────────┐         ┌──────────────────┐             │
│              │    ESP32     │────────→│  SIM800L v2.0    │             │
│              │  DevKit V1   │  UART   │  GSM/GPRS Module │             │
│              │              │         │  + Antena         │             │
│              └──────┬───────┘         └──────────────────┘             │
│                     │                                                   │
│  ┌──────────────────┴──────────────────────────────┐                   │
│  │              SUBSISTEM CATU DAYA                 │                   │
│  │  Solar Panel 12V → CN3791 → BMS 1S → 2×18650   │                   │
│  │                                → MT3608 → ESP32  │                   │
│  │                                → XL6009 → SIM800L│                   │
│  └──────────────────────────────────────────────────┘                   │
└─────────────────────────────────────────────────────────────────────────┘
                              │
                              │ GPRS / Internet
                              ▼
                 ┌───────────────────────┐
                 │  Firebase Realtime    │
                 │  Database (Cloud)     │
                 │  ┌─────────────────┐  │
                 │  │ /smart_buoy/    │  │
                 │  │   ├── live/     │  │ ← Real-time stream
                 │  │   └── history/  │  │ ← Time-series data lake
                 │  └─────────────────┘  │
                 └───────────┬───────────┘
                             │
                    ┌────────┴────────┐
                    ▼                 ▼
          ┌──────────────┐   ┌──────────────┐
          │ Flutter App  │   │ Web Dashboard│
          │ (Mobile)     │   │ (index.html) │
          └──────────────┘   └──────────────┘
```

---

## 3. Arsitektur Perangkat Keras

### 3.1 Daftar Komponen

| No | Komponen | Tipe/Model | Spesifikasi | Fungsi |
|----|----------|-----------|-------------|--------|
| 1 | Mikrokontroler | DOIT ESP32 DevKit V1 | Dual-core 240MHz, WiFi, BT | Pemrosesan data & komunikasi |
| 2 | Sensor Suhu | DS18B20 Waterproof | -55°C ~ +125°C, ±0.5°C | Pengukuran suhu air |
| 3 | Sensor pH | Analog pH Sensor Module | 0–14 pH, output 0–3.3V | Pengukuran keasaman air |
| 4 | Sensor Kekeruhan | Analog Turbidity Sensor | Output analog 0–4.5V | Pengukuran kejernihan air |
| 5 | Modul GSM | SIM800L v2.0 Quad Band | GSM 850/900/1800/1900 MHz | Komunikasi seluler GPRS |
| 6 | Antena | SMA External Antenna | Gain ~3dBi | Penguatan sinyal GSM |

### 3.2 Konfigurasi Pin ESP32

| GPIO | Mode | Peripheral | Catatan |
|------|------|-----------|---------|
| GPIO 4 | Digital (OneWire) | DS18B20 | Pull-up 4.7kΩ diperlukan |
| GPIO 34 | Analog Input (ADC1) | Sensor pH | ADC1 — aman digunakan bersama WiFi |
| GPIO 35 | Analog Input (ADC1) | Sensor Turbidity | ADC1 — aman digunakan bersama WiFi |
| GPIO 16 | UART2 RX | SIM800L TX | Komunikasi serial ke modem |
| GPIO 17 | UART2 TX | SIM800L RX | Komunikasi serial ke modem |

> **Catatan Teknis:** Pin ADC2 (GPIO 0, 2, 12, 13, 14, 15, 25, 26, 27) **tidak boleh** digunakan untuk pembacaan analog saat WiFi aktif karena konflik internal pada ESP32. Semua sensor analog ditempatkan pada kanal **ADC1**.

---

## 4. Arsitektur Catu Daya

### 4.1 Diagram Aliran Daya

```
┌───────────────────┐
│   SOLAR PANEL     │
│   12V / 2W        │
│   (Polycrystalline)│
└────────┬──────────┘
         │ 12V DC
         ▼
┌───────────────────┐
│   CN3791          │   ← Solar MPPT Charge Controller
│   Solar Charger   │     Input: 4.5V – 28V
│   Module (12V)    │     Charge Voltage: 4.2V (Li-Ion)
│                   │     Efisiensi: ~90%
└────────┬──────────┘
         │ 4.2V (charging) / 3.0–4.2V (discharging)
         ▼
┌───────────────────┐
│   BMS 1S          │   ← Battery Management System
│   Protection      │     Overcharge Protection: 4.25V
│   Module          │     Overdischarge Protection: 2.5V
│                   │     Short Circuit Protection: ✓
└────────┬──────────┘
         │
         ▼
┌───────────────────┐
│  2× 18650 Li-Ion  │   ← Konfigurasi PARALEL (1S2P)
│  PARALEL          │     Tegangan: 3.7V nominal
│                   │     Kapasitas: 3350mAh × 2 = 6700 mAh
│  Total: 3.7V      │     Energi: 6700 × 3.7 = 24.79 Wh
│  6700 mAh         │
└───┬───────────┬───┘
    │           │
    ▼           ▼
┌────────┐   ┌───────────────────────────────────────────┐
│ BMS 1S │   │  XL6009 DC-DC Boost Converter             │
│(output)│   │  Input: 3.0–4.2V → Output: ~4.0V          │
└───┬────┘   │  Max Current: 4A                           │
    │        └──────────────┬────────────────────────────┘
    ▼                       ▼
┌────────┐   ┌───────────────────────────────────────────┐
│ MT3608 │   │  Kapasitor Elektrolit 1000µF / 16V        │
│ Boost  │   │  Fungsi: Meredam lonjakan arus (inrush    │
│ 3.7V→5V│   │  current) SIM800L saat transmit RF        │
│        │   │  Peak burst SIM800L: ~2A selama ~577µs    │
└───┬────┘   └──────────────┬────────────────────────────┘
    │                       │
    ▼                       ▼
┌────────┐   ┌──────────────────────┐
│ ESP32  │   │ SIM800L v2.0         │
│  +     │   │ Quad Band GSM/GPRS   │
│Sensors │   │ + Antena Eksternal   │
└────────┘   └──────────────────────┘
```

### 4.2 Spesifikasi Komponen Daya

| No | Komponen | Model | Input | Output | Fungsi |
|----|----------|-------|-------|--------|--------|
| 1 | Solar Panel | Polycrystalline | Cahaya matahari | 12V / 2W | Sumber energi utama |
| 2 | Charge Controller | CN3791 | 4.5–28V DC | 4.2V CC/CV | MPPT solar charger untuk Li-Ion |
| 3 | BMS (Input) | 1S Li-Ion BMS | 3.0–4.2V | 3.0–4.2V | Proteksi sel baterai saat pengisian |
| 4 | Baterai | 18650 Li-Ion ×2 | 4.2V (charge) | 3.7V nom. | Penyimpanan energi 6700mAh |
| 5 | BMS (Output) | 1S Li-Ion BMS | 3.0–4.2V | 3.0–4.2V | Proteksi sel baterai saat pemakaian |
| 6 | Boost (ESP32) | MT3608 | 2–24V | 5V DC | Step-up untuk ESP32 (Vin pin) |
| 7 | Boost (SIM800L) | XL6009 | 3–32V | ~4.0V DC | Step-up untuk modul GSM |
| 8 | Kapasitor | Elektrolit | — | — | Stabilisasi arus burst SIM800L |

### 4.3 Analisis Konsumsi Daya

| Komponen | Mode Aktif | Deep Sleep | Tegangan | Daya Aktif |
|----------|-----------|-----------|----------|-----------|
| ESP32 (UART TX) | ~100 mA | ~0.01 mA | 3.3V | ~330 mW |
| DS18B20 | ~1.5 mA | ~1 µA | 3.3V | ~5 mW |
| Sensor pH | ~5 mA | ~5 mA | 5.0V | ~25 mW |
| Sensor Turbidity | ~30 mA | ~30 mA | 5.0V | ~150 mW |
| SIM800L (TX Burst) | ~2000 mA | ~18 mA | 4.0V | ~8000 mW |
| SIM800L (GPRS Idle) | ~18 mA | ~18 mA | 4.0V | ~72 mW |

### 4.4 Keseimbangan Energi Harian (Dengan Deep Sleep)

Sistem menggunakan **deep sleep mode** dengan interval 2 menit. ESP32 hanya aktif selama ~8 detik per siklus (baca sensor + kirim data via GPRS).

```
═══════════════════════════════════════════════════
  ENERGI MASUK (Solar Harvesting)
═══════════════════════════════════════════════════
  Daya panel (peak)        : 2W
  Jam matahari efektif     : ~4 jam/hari (Indonesia Timur)
  Efisiensi CN3791         : ~90%
  Efisiensi kondisi riil   : ~70% (sudut panel, cuaca, debu)
  ─────────────────────────────────────────────────
  Energi masuk/hari        ≈ 2W × 4h × 0.7 = 5.6 Wh

═══════════════════════════════════════════════════
  ENERGI KELUAR (System Load — Deep Sleep Mode)
═══════════════════════════════════════════════════
  Duty Cycle:
    Aktif  : ~8 detik per siklus (baca + kirim)
    Tidur  : ~112 detik per siklus
    Rasio  : 8/120 = ~6.7%

  ESP32 (rata-rata dengan deep sleep):
    I_avg = (100mA × 8s + 0.01mA × 112s) / 120s = ~6.7 mA
    P_avg = 6.7mA × 3.7V = ~24.8 mW

  SIM800L (selalu aktif, idle saat ESP32 tidur):
    I_avg = ~20 mA (idle dominan, burst sesaat)
    P_avg = 20mA × 4.0V = ~80 mW

  Sensor (aktif hanya saat ESP32 aktif):
    I_avg ≈ (36.5mA × 8s) / 120s = ~2.4 mA
    P_avg ≈ ~10 mW

  Losses boost converter   : ~15%
  ─────────────────────────────────────────────────
  Total beban efektif      ≈ (24.8 + 80 + 10) / 0.85 = ~135 mW
  Energi keluar/hari       ≈ 0.135W × 24h = 3.24 Wh

═══════════════════════════════════════════════════
  NERACA ENERGI
═══════════════════════════════════════════════════
  Energi masuk             : 5.6 Wh/hari
  Energi keluar            : 3.24 Wh/hari
  Selisih                  : +2.36 Wh/hari (SURPLUS) ✅

  Kapasitas baterai        : 24.79 Wh
  Runtime tanpa solar      : 24.79 / 0.135 ≈ 183.6 jam ≈ 7.7 hari
  Dengan solar             : INDEFINITE (surplus harian) ♾️
```

> **Kesimpulan:** Dengan implementasi deep sleep, sistem mencapai **surplus energi harian +2.36 Wh**, memungkinkan operasi **tanpa batas waktu** selama panel surya menerima cahaya minimal ~4 jam per hari. Bahkan dalam kondisi mendung berturut-turut, baterai 6700mAh mampu menopang operasi selama ~7.7 hari tanpa pengisian.

### 4.5 Peran Kapasitor 1000µF pada SIM800L

SIM800L memiliki karakteristik konsumsi arus yang unik saat transmisi RF:

```
Profil Arus SIM800L (satu siklus TDMA burst):
  ───────────────────────────────────────────
  │       ┌──┐                              │
  │ ~2A   │  │  ← RF Burst (~577µs)        │
  │       │  │                              │
  │  ─────┘  └────────── ~18mA (idle)       │
  ───────────────────────────────────────────
  Periode TDMA: 4.615ms (1 dari 8 time slot)
```

Tanpa kapasitor, lonjakan arus 2A dapat menyebabkan:
- **Voltage drop** drastis pada baterai/boost converter
- ESP32 **reset spontan** akibat tegangan turun di bawah threshold
- **Kegagalan transmisi** data ke jaringan GSM

Kapasitor 1000µF berfungsi sebagai **energy buffer** yang menyuplai arus instan saat burst, kemudian diisi kembali saat idle.

---

## 5. Arsitektur Firmware

### 5.1 Struktur File

```
final_project/
├── final_project.ino    ← Entry point: SIM800L, Firebase REST API, deep sleep
├── Config.h             ← Konfigurasi pin, APN, deep sleep, konstanta
├── Sensors.h            ← Header: deklarasi fungsi sensor
├── Sensors.cpp          ← Implementasi: ADC, kalibrasi, pembacaan
└── KALIBRASI.md         ← Dokumentasi prosedur kalibrasi pH
```

### 5.2 Alur Eksekusi Firmware (Deep Sleep Model)

Firmware menggunakan model eksekusi **wake-execute-sleep**. Setiap kali bangun dari deep sleep, `setup()` dijalankan dari awal. `loop()` hanya digunakan saat mode kalibrasi.

```
              ┌──────────────────────┐
              │  WAKE UP / POWER ON  │ ← RTC Timer atau power-on reset
              └──────────┬───────────┘
                         ▼
              ┌──────────────────────┐
              │  setup()             │
              │  1. Init sensor      │
              │  2. Load NVS pH Cal  │
              │  3. Tunggu 5 detik   │──→ Ada input serial?
              └──────────┬───────────┘         │
                         │ Tidak               ▼ Ya
                         ▼            ┌────────────────────┐
              ┌──────────────────┐    │  CALIBRATION MODE  │
              │  4. Baca sensor  │    │  loop() aktif      │
              │  pH, Suhu, Turb  │    │  CAL4/CAL7/CALSAVE │
              └──────────┬───────┘    │  Ketik EXIT untuk  │
                         ▼            │  kembali normal    │
              ┌──────────────────┐    └────────────────────┘
              │  5. Init SIM800L │
              │  AT Commands     │
              │  Open GPRS       │
              └──────────┬───────┘
                         ▼
              ┌──────────────────┐
              │  6. Kirim /live  │ ← HTTPS PUT via SIM800L
              │  (jika Δ > thr) │    Firebase REST API
              └──────────┬───────┘
                         ▼
              ┌──────────────────┐
              │  7. Boot ke-5?   │──→ Ya: Kirim /history (HTTPS POST)
              │  (setiap 10 mnt) │      dengan timestamp dari AT+CCLK
              └──────────┬───────┘
                         ▼
              ┌──────────────────┐
              │  8. Close GPRS   │
              │  9. DEEP SLEEP   │ ← esp_deep_sleep_start()
              │     2 menit      │    Konsumsi: ~10 µA
              └──────────┬───────┘
                         │
                         └──→ RTC Timer → Kembali ke WAKE UP
```

**Variabel RTC Memory** (bertahan saat deep sleep):
- `bootCount` — Menghitung jumlah siklus bangun
- `lastSentPH`, `lastSentTemp`, `lastSentTurb` — Nilai terakhir yang dikirim (untuk threshold check)

### 5.3 Pemrosesan Sinyal ADC (Trimmed Mean Filter)

Untuk meminimalkan noise pada pembacaan ADC sensor pH, firmware menggunakan metode **Trimmed Mean Filter**:

```
Langkah:
  1. Ambil 10 sampel ADC (interval 10ms per sampel)
  2. Urutkan ascending
  3. Buang 2 sampel tertinggi + 2 terendah (outlier)
  4. Rata-ratakan 6 sampel sisanya
  5. Konversi ke tegangan: V = ADC × (3.3 / 4095)

Contoh:
  Raw    : [2890, 2905, 2856, 2912, 2895, 2988, 2901, 2898, 2843, 2910]
  Sorted : [2843, 2856, 2890, 2895, 2898, 2901, 2905, 2910, 2912, 2988]
  Trimmed: [────, ────, 2890, 2895, 2898, 2901, 2905, 2910, ────, ────]
  Mean   : 2899.8
  Voltage: 2899.8 × (3.3 / 4095) = 2.336V
```

### 5.4 Kalibrasi pH (Two-Point Linear)

Sistem menggunakan kalibrasi dua titik dengan larutan buffer pH 4.01 dan pH 6.86:

```
Persamaan kalibrasi:
  pH = (m × V) + b

Dimana:
  m = (6.86 - 4.01) / (V_pH7 - V_pH4)    ← Slope
  b = 4.01 - (m × V_pH4)                  ← Intercept

Kompensasi Suhu (Nernst):
  pH_terkoreksi = pH_mentah + 0.003 × (Suhu - 25.0°C)
```

Nilai kalibrasi (m dan b) disimpan secara permanen di **NVS (Non-Volatile Storage)** flash memory ESP32 dan tetap bertahan meskipun perangkat dimatikan.

---

## 6. Arsitektur Data & Telemetri

### 6.1 Dual Pipeline Architecture

Sistem menggunakan dua jalur data terpisah ke Firebase Realtime Database:

```
┌─────────────────────────────────────────────────────────────┐
│                    FIREBASE RTDB                             │
│                                                              │
│  /smart_buoy/                                                │
│     │                                                        │
│     ├── live/                    [REAL-TIME STREAM]           │
│     │   ├── pH   : 7.23         Metode  : SET (overwrite)    │
│     │   ├── temp : 28.5         Interval: 10 detik           │
│     │   └── turb : 1420         Trigger : Delta threshold    │
│     │                                                        │
│     └── history/                 [TIME-SERIES DATA LAKE]     │
│         ├── -NxAbc123/          Metode  : PUSH (append)      │
│         │   ├── pH   : 7.23    Interval: 10 menit            │
│         │   ├── temp : 28.5    Trigger : Cron timer           │
│         │   ├── turb : 1420                                   │
│         │   └── ts   : 1746201600  ← Unix Epoch              │
│         ├── -NxDef456/                                        │
│         │   └── ...                                           │
│         └── ...                                               │
└─────────────────────────────────────────────────────────────┘
```

### 6.2 Optimasi Bandwidth (Threshold-Based Transmission)

Data live **hanya dikirim** jika perubahan melebihi threshold:

| Parameter | Threshold | Alasan |
|-----------|-----------|--------|
| Suhu | Δ > 0.1°C | Perubahan di bawah 0.1°C tidak signifikan secara biologis |
| pH | Δ > 0.05 | Resolusi sensor analog terbatas |
| Kekeruhan | Δ > 5 ADC | Noise ADC tipikal ±3 digit |

Estimasi penghematan bandwidth: **hingga 80%** pada kondisi air stabil.

### 6.3 Klasifikasi Kualitas Air

| Parameter | Kategori | Rentang | Keterangan |
|-----------|----------|---------|------------|
| Kekeruhan | Jernih | ADC < 1500 | Air dalam kondisi baik |
| Kekeruhan | Keruh | 1500 ≤ ADC < 3000 | Perlu perhatian |
| Kekeruhan | Kotor | ADC ≥ 3000 | Tindakan segera diperlukan |
| pH | Normal | 6.5 – 8.5 | Rentang aman untuk akuakultur |
| pH | Bahaya | < 6.5 atau > 8.5 | Di luar toleransi biota |
| Suhu | Optimal | 25°C – 32°C | Rentang ideal tambak udang |
| Suhu | Perlu Cek | < 25°C atau > 32°C | Potensi stres pada biota |

---

## 7. Arsitektur Komunikasi

### 7.1 Topologi Jaringan

```
┌──────────┐   SIM800L GPRS   ┌───────────┐     HTTPS      ┌──────────┐
│  Smart   │   (AT Commands)  │  Jaringan │  (REST API)    │ Firebase │
│  Buoy    │ ────────────────│  Seluler   │ ────────────── │  RTDB    │
│  (ESP32) │                   │  2G/GPRS  │                │  (Cloud) │
└──────────┘                   └─────┬──────┘                └────┬─────┘
                                     │                            │
                                     │                   ┌────────┴────────┐
                                     │                   ▼                 ▼
                                     │          ┌──────────────┐  ┌──────────────┐
                                     └─────────│ Flutter App  │  │ Web Dashboard│
                                                │ (Real-time)  │  │ (Real-time)  │
                                                └──────────────┘  └──────────────┘
```

### 7.2 Protokol Komunikasi

| Layer | Protokol | Keterangan |
|-------|----------|------------|
| Transport | TCP/IP over GPRS (SIM800L) | Koneksi seluler 2G Quad Band |
| Application | Firebase REST API (HTTPS) | PUT untuk /live, POST untuk /history |
| Modem Control | AT Commands (UART) | Kontrol SIM800L via HardwareSerial |
| Time Sync | AT+CCLK (Network Time) | Waktu dari jaringan seluler GSM |
| Sensor | OneWire (DS18B20) | Digital, single-wire bus |
| Sensor | ADC 12-bit (pH, Turbidity) | Analog-to-Digital, 0–4095 |

---

## 8. Ringkasan Spesifikasi Sistem

| Aspek | Spesifikasi |
|-------|-------------|
| **MCU** | ESP32 Dual-core Xtensa LX6, 240 MHz, 520KB SRAM |
| **Parameter Monitoring** | Suhu, pH, Kekeruhan |
| **Power Mode** | Deep Sleep (2 menit interval, ~10 µA saat tidur) |
| **Interval Polling** | 2 menit (live), 10 menit (history) |
| **Database** | Firebase Realtime Database (REST API) |
| **Baterai** | 2× 18650 Li-Ion Paralel, 6700mAh, 3.7V (24.79 Wh) |
| **Solar** | 12V / 2W Polycrystalline |
| **Charge Controller** | CN3791 MPPT |
| **Kalibrasi pH** | Two-Point Linear, Nernst Temperature Compensation |
| **Penyimpanan Kalibrasi** | ESP32 NVS Flash (Non-Volatile) |
| **Filter ADC** | Trimmed Mean (10 sampel, trim 2 atas + 2 bawah) |
| **Konektivitas** | SIM800L v2.0 GPRS (GSM Quad Band 850/900/1800/1900 MHz) |
| **Runtime Tanpa Solar** | ~7.7 hari (183.6 jam) |
| **Runtime Dengan Solar** | Indefinite (surplus +2.36 Wh/hari) |
| **Client** | Flutter Mobile App + Web Dashboard (Firebase SDK) |

---

*Dokumen ini adalah bagian dari dokumentasi Smart Buoy IoT System.*
*Terakhir diperbarui: Mei 2026*
