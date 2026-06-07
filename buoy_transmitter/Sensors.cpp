/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Edi Loupatty
 *
 * This file is part of the Smart Buoy IoT System.
 */

/**
 * @file Sensors.cpp
 * @brief Implementation of sensor polling, calibration, and signal processing logic.
 *
 * pH Calibration Method: Two-Point Linear Calibration with Nernst Temperature Compensation.
 * Calibration values are persisted in ESP32 NVS (Non-Volatile Storage) flash memory.
 *
 * Turbidity Calibration Method: Two-Point Linear (HARDCODED, not stored in NVS).
 * Anchors: TURB_V_CLEAR (0 NTU) → TURB_V_TURBID (TURB_NTU_MAX). SEN0189: makin keruh,
 * tegangan makin turun. Ubah konstanta lalu re-flash untuk mengganti kalibrasi.
 */

#include "Sensors.h"
#include "Config.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>  // ESP32 NVS (Non-Volatile Storage) for calibration persistence
#include <Wire.h>
#include <Adafruit_ADS1X15.h>  // ADC eksternal 16-bit untuk turbidity

// DS18B20 Temperature Sensor Instance
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

// ADS1115 — ADC eksternal 16-bit (I2C) untuk turbidity
Adafruit_ADS1115 ads;
bool adsReady = false;  ///< true bila ADS1115 terdeteksi di bus I2C

// NVS Storage Handle
Preferences preferences;

// ── pH Calibration State ──────────────────────────────────────────────────────
// Default voltage values at given reference pH (roughly 2.5V at pH 7, ~-0.18V/pH)
float calV4 = 3.1934f;   ///< Voltage at pH 4.01
float calV7 = 2.6890f;   ///< Voltage at pH 6.86

// Temporary calibration buffer (populated during interactive calibration session)
float cal4Voltage = 0.0f;  ///< Recorded voltage at pH 4.01 buffer
float cal7Voltage = 0.0f;  ///< Recorded voltage at pH 6.86 buffer
bool  cal4Set     = false;  ///< Flag: pH 4.01 point captured
bool  cal7Set     = false;  ///< Flag: pH 6.86 point captured

// Reference pH values for standard buffer solutions
const float PH_REF_4 = 4.01f;
const float PH_REF_7 = 6.86f;

// ── Turbidity Calibration (2-Point, HARDCODED) — dibaca via ADS1115 ─────────────
// SEN0189: turbiditas naik => voltase turun. Garis: (V_CLEAR, 0 NTU) → (V_TURBID, MAX).
// PENTING: nilai di bawah ini masih dari ADC internal lama (saturasi 3.3V). ADS1115
// membaca tegangan ASLI (bisa >3.3V), jadi WAJIB DIUKUR ULANG:
//   1) celup probe di air jernih → ketik TURBV di Serial → catat V → isi TURB_V_CLEAR
//   2) celup di air keruh 400 NTU → TURBV → catat V → isi TURB_V_TURBID
//   3) re-flash. (Lihat NOTE_KALIBRASI.md)
const float TURB_V_CLEAR  = 3.45f;   ///< Voltage pada 0 NTU (air jernih)  — UKUR ULANG
const float TURB_V_TURBID = 2.26f;   ///< Voltage pada TURB_NTU_MAX (keruh) — UKUR ULANG
const float TURB_NTU_MAX  = 400.0f;  ///< NTU pada titik keruh

// ── ADC Sampling Configuration ────────────────────────────────────────────────
const int   ADC_SAMPLES     = 10;    ///< Total samples per reading
const int   ADC_TRIM_COUNT  = 2;     ///< Samples to discard from each end (high & low)
const int   ADC_SAMPLE_DELAY_MS = 10; ///< Delay between consecutive ADC samples (ms)

// ──────────────────────────────────────────────────────────────────────────────
// Initialization
// ──────────────────────────────────────────────────────────────────────────────

void initSensors() {
  // Configure ESP32 ADC attenuation for the 0-3.3V measurement range (pH masih ADC internal)
  analogSetAttenuation(ADC_11db);
  sensors.begin();

  // ── ADS1115 (ADC eksternal untuk turbidity) ──
  Wire.begin(I2C_SDA, I2C_SCL);
  adsReady = ads.begin(ADS1115_ADDR);
  if (adsReady) {
    // ±6.144V → bisa baca s/d ~5V (turbidity di-supply 5V). 1 LSB ≈ 0.1875 mV.
    ads.setGain(GAIN_TWOTHIRDS);
    Serial.println("[ADS1115] ✓ Terdeteksi — turbidity dibaca via ADC eksternal (A0)");
  } else {
    Serial.println("[ADS1115] ✗ TIDAK terdeteksi — cek wiring I2C (SDA21/SCL22/VDD 5V/ADDR→GND)");
  }
}

void loadCalibration() {
  preferences.begin("ph_cal", true);  // Read-only mode
  calV4 = preferences.getFloat("v4", 3.1934f);
  calV7 = preferences.getFloat("v7", 2.6890f);
  preferences.end();

  Serial.println("═══════════════════════════════════════");
  Serial.println("  Sensor Calibration Loaded");
  Serial.println("  -- pH (2-Point, dari NVS) --");
  Serial.printf("  V(pH 4.01) = %.4f V\n", calV4);
  Serial.printf("  V(pH 6.86) = %.4f V\n", calV7);
  Serial.println("  -- Turbidity (2-Point, HARDCODED) --");
  Serial.printf("  V(0   NTU) = %.4f V\n", TURB_V_CLEAR);
  Serial.printf("  V(%.0f NTU) = %.4f V\n", TURB_NTU_MAX, TURB_V_TURBID);
  Serial.println("═══════════════════════════════════════");
}

// ──────────────────────────────────────────────────────────────────────────────
// ADC Signal Processing: Trimmed Mean Filter
// ──────────────────────────────────────────────────────────────────────────────

/**
 * @brief Reads multiple ADC samples, removes outliers, and returns a trimmed mean voltage.
 * 
 * Process:
 * 1. Collect ADC_SAMPLES readings with short delays
 * 2. Sort the readings in ascending order
 * 3. Discard ADC_TRIM_COUNT samples from top and bottom
 * 4. Average the remaining (ADC_SAMPLES - 2 * ADC_TRIM_COUNT) samples
 * 5. Convert to voltage (3.3V / 4095 for 12-bit ADC)
 * 
 * @param pin The analog GPIO pin to sample.
 * @return Filtered voltage (float) in the range 0.0 - 3.3V.
 */
static float readTrimmedMeanVoltage(int pin) {
  int samples[ADC_SAMPLES];

  // Collect raw ADC samples
  for (int i = 0; i < ADC_SAMPLES; i++) {
    samples[i] = analogRead(pin);
    delay(ADC_SAMPLE_DELAY_MS);
  }

  // Simple insertion sort (efficient for small N)
  for (int i = 1; i < ADC_SAMPLES; i++) {
    int key = samples[i];
    int j = i - 1;
    while (j >= 0 && samples[j] > key) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = key;
  }

  // Average the middle samples (trimmed mean)
  long sum = 0;
  int validCount = ADC_SAMPLES - (2 * ADC_TRIM_COUNT);
  for (int i = ADC_TRIM_COUNT; i < ADC_SAMPLES - ADC_TRIM_COUNT; i++) {
    sum += samples[i];
  }

  float avgAdc = (float)sum / validCount;
  return avgAdc * (3.3f / 4095.0f);
}

/**
 * @brief Trimmed-mean voltase turbidity dari ADS1115 (kanal TURB_ADS_CHANNEL).
 *        Sama prinsipnya dengan readTrimmedMeanVoltage tapi sumbernya ADC eksternal
 *        16-bit (bisa baca >3.3V). computeVolts() sudah memperhitungkan gain.
 * @return Voltase tersaring (float). -1.0f bila ADS1115 tidak siap.
 */
static float readTurbidityVoltageADS() {
  if (!adsReady) return -1.0f;

  float samples[ADC_SAMPLES];
  for (int i = 0; i < ADC_SAMPLES; i++) {
    int16_t raw = ads.readADC_SingleEnded(TURB_ADS_CHANNEL);
    samples[i]  = ads.computeVolts(raw);
    delay(ADC_SAMPLE_DELAY_MS);
  }

  // Insertion sort (N kecil)
  for (int i = 1; i < ADC_SAMPLES; i++) {
    float key = samples[i];
    int j = i - 1;
    while (j >= 0 && samples[j] > key) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = key;
  }

  // Rata-rata bagian tengah (buang ADC_TRIM_COUNT di tiap ujung)
  float sum = 0.0f;
  int validCount = ADC_SAMPLES - (2 * ADC_TRIM_COUNT);
  for (int i = ADC_TRIM_COUNT; i < ADC_SAMPLES - ADC_TRIM_COUNT; i++) {
    sum += samples[i];
  }
  return sum / validCount;
}

// ──────────────────────────────────────────────────────────────────────────────
// pH Sensor Reading
// ──────────────────────────────────────────────────────────────────────────────

float readPH() {
  float voltage = readTrimmedMeanVoltage(PH_PIN);
  
  // Guard against division by zero
  if (abs(calV7 - calV4) < 0.001f) return 7.0f;

  // Linear Regression (2-Point)
  float m = (PH_REF_7 - PH_REF_4) / (calV7 - calV4);
  float b = PH_REF_7 - (m * calV7);
  
  float ph = (m * voltage) + b;
  
  // Clamp to reasonable bounds
  if (ph < 0.0f) ph = 0.0f;
  if (ph > 14.0f) ph = 14.0f;
  
  return ph;
}

float readPH(float tempC) {
  float rawPH = readPH();

  // Nernst Temperature Compensation
  // pH electrodes are calibrated at 25°C reference temperature.
  // Deviation from 25°C introduces a systematic offset of ~0.003 pH/°C.
  if (tempC > -126.0f) {  // Guard: only compensate if temperature reading is valid
    rawPH += 0.003f * (tempC - 25.0f);
  }

  return rawPH;
}

// ──────────────────────────────────────────────────────────────────────────────
// Temperature Sensor Reading
// ──────────────────────────────────────────────────────────────────────────────

float readTemperature() {
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);

  // Guard against hardware disconnection (-127.0 is the DS18B20 default error value)
  if (tempC <= -126.0f) {
    return -127.0f;
  }
  return tempC;
}

// ──────────────────────────────────────────────────────────────────────────────
// Turbidity Sensor Reading
// ──────────────────────────────────────────────────────────────────────────────
// 2-Point linear (hardcoded): (TURB_V_CLEAR, 0 NTU) → (TURB_V_TURBID, TURB_NTU_MAX).
// SEN0189: makin keruh, voltase makin turun → slope m negatif.

float readTurbidityNTU() {
  float voltage = readTurbidityVoltageADS();
  if (voltage < 0.0f) return -1.0f;  // ADS1115 tidak siap → tandai fault (-1 NTU)

  // m = ΔNTU / ΔV  (negatif karena voltase turun saat NTU naik)
  float m   = TURB_NTU_MAX / (TURB_V_TURBID - TURB_V_CLEAR);
  float ntu = m * (voltage - TURB_V_CLEAR);

  // Clamp ke rentang fisik yang masuk akal
  if (ntu < 0.0f)    ntu = 0.0f;
  if (ntu > 3000.0f) ntu = 3000.0f;
  return ntu;
}

String getTurbidityStatus(float ntuValue) {
  // Ambang kualitatif kejernihan air
  if (ntuValue < 25.0f)  return "Jernih";
  if (ntuValue < 100.0f) return "Keruh";
  return "Sangat Keruh";
}

// ──────────────────────────────────────────────────────────────────────────────
// Interactive Serial Calibration Engine
// ──────────────────────────────────────────────────────────────────────────────

void handleCalibrationCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();

  // ── CAL4: Record voltage at pH 4.01 buffer ──
  if (cmd == "CAL4") {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║   KALIBRASI pH 4.01 — Sampling...    ║");
    Serial.println("╚══════════════════════════════════════╝");

    cal4Voltage = readTrimmedMeanVoltage(PH_PIN);
    cal4Set = true;

    Serial.printf("  ✓ Tegangan tercatat: %.4f V\n", cal4Voltage);
    Serial.printf("  ✓ ADC ~ %d\n", (int)(cal4Voltage / 3.3f * 4095.0f));
    Serial.println("  → Lanjutkan: celupkan ke buffer pH 6.86, lalu ketik CAL7");

  // ── CAL7: Record voltage at pH 6.86 buffer ──
  } else if (cmd == "CAL7") {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║   KALIBRASI pH 6.86 — Sampling...    ║");
    Serial.println("╚══════════════════════════════════════╝");

    cal7Voltage = readTrimmedMeanVoltage(PH_PIN);
    cal7Set = true;

    Serial.printf("  ✓ Tegangan tercatat: %.4f V\n", cal7Voltage);
    Serial.printf("  ✓ ADC ~ %d\n", (int)(cal7Voltage / 3.3f * 4095.0f));
    Serial.println("  → Lanjutkan: ketik CALSAVE untuk menghitung & menyimpan kalibrasi");

  // ── CALSAVE: Persist to NVS ──
  } else if (cmd == "CALSAVE") {
    if (!cal4Set || !cal7Set) {
      Serial.println("\n  ✗ ERROR: Kedua titik kalibrasi (4.01, 6.86) harus diambil terlebih dahulu!");
      Serial.println("    Jalankan CAL4 dan CAL7 sebelum CALSAVE.");
      return;
    }

    // Basic sanity check on voltages
    if (abs(cal7Voltage - cal4Voltage) < 0.001f) {
      Serial.println("\n  ✗ ERROR: Tegangan antar buffer terlalu mirip!");
      Serial.println("    Pastikan sensor tercelup dengan benar di masing-masing larutan buffer.");
      return;
    }

    // Persist to NVS
    preferences.begin("ph_cal", false);  // Read-write mode
    preferences.putFloat("v4", cal4Voltage);
    preferences.putFloat("v7", cal7Voltage);
    preferences.end();

    // Update runtime values
    calV4 = cal4Voltage;
    calV7 = cal7Voltage;

    // Reset calibration session
    cal4Set = false;
    cal7Set = false;

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║    ✓ KALIBRASI BERHASIL DISIMPAN     ║");
    Serial.println("╠══════════════════════════════════════╣");
    Serial.printf("║  V(pH4.01) = %.4f V                 ║\n", calV4);
    Serial.printf("║  V(pH6.86) = %.4f V                 ║\n", calV7);
    Serial.println("╠══════════════════════════════════════╣");
    Serial.println("║  Data tersimpan di NVS Flash.        ║");
    Serial.println("║  Sistem menggunakan 2-Point Linear.  ║");
    Serial.println("╚══════════════════════════════════════╝");

  // ── TURBV: Baca tegangan turbidity dari ADS1115 (untuk re-kalibrasi) ──
  } else if (cmd == "TURBV") {
    float v = readTurbidityVoltageADS();
    if (v < 0.0f) {
      Serial.println("\n  ✗ ADS1115 tidak siap — cek wiring I2C.");
    } else {
      Serial.println("\n╔══════════════════════════════════════╗");
      Serial.println("║   KALIBRASI TURBIDITY (ADS1115)      ║");
      Serial.println("╚══════════════════════════════════════╝");
      Serial.printf("  Tegangan (A%d) = %.4f V\n", TURB_ADS_CHANNEL, v);
      Serial.printf("  NTU saat ini  = %.1f\n", readTurbidityNTU());
      Serial.println("  → Air JERNIH : catat V ini ke TURB_V_CLEAR");
      Serial.println("  → Air 400 NTU: catat V ini ke TURB_V_TURBID, lalu re-flash.");
    }

  // ── CALINFO: Display current calibration status ──
  } else if (cmd == "CALINFO") {
    Serial.println("\n═══════════════════════════════════════");
    Serial.println("  STATUS KALIBRASI SENSOR");
    Serial.println("═══════════════════════════════════════");
    Serial.println("  -- pH (2-Point) --");
    Serial.printf("  V(pH4.01) = %.4f V\n", calV4);
    Serial.printf("  V(pH6.86) = %.4f V\n", calV7);
    Serial.println("  -- Turbidity (2-Point, HARDCODED) --");
    Serial.printf("  V(0   NTU) = %.4f V\n", TURB_V_CLEAR);
    Serial.printf("  V(%.0f NTU) = %.4f V\n", TURB_NTU_MAX, TURB_V_TURBID);
    Serial.printf("  Sesi aktif: CAL4=%s, CAL7=%s\n",
                  cal4Set ? "✓" : "—", cal7Set ? "✓" : "—");
    if (cal4Set) Serial.printf("  V(pH4_temp) = %.4f V\n", cal4Voltage);
    if (cal7Set) Serial.printf("  V(pH7_temp) = %.4f V\n", cal7Voltage);
    Serial.println("═══════════════════════════════════════");
    Serial.println("  pH:        CAL4 | CAL7 | CALSAVE");
    Serial.println("  Turbidity: TURBV  (baca V untuk re-kalibrasi)");
    Serial.println("  Info:      CALINFO");
    Serial.println("═══════════════════════════════════════");
  }
}