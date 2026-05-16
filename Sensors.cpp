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
 */

#include "Sensors.h"
#include "Config.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>  // ESP32 NVS (Non-Volatile Storage) for calibration persistence

// DS18B20 Temperature Sensor Instance
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

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

// ── Turbidity Calibration State ───────────────────────────────────────────────
float calTurbV0 = 3.3f;    ///< Voltage at 0 NTU (Clear Water)
float calTurbV400 = 1.0f;  ///< Voltage at 400 NTU (Turbid Water)

float calTurb0Voltage = 0.0f;
float calTurb400Voltage = 0.0f;
bool  calTurb0Set     = false;
bool  calTurb400Set   = false;

const float TURB_REF_0   = 0.0f;
const float TURB_REF_400 = 400.0f;

// ── ADC Sampling Configuration ────────────────────────────────────────────────
const int   ADC_SAMPLES     = 10;    ///< Total samples per reading
const int   ADC_TRIM_COUNT  = 2;     ///< Samples to discard from each end (high & low)
const int   ADC_SAMPLE_DELAY_MS = 10; ///< Delay between consecutive ADC samples (ms)

// ──────────────────────────────────────────────────────────────────────────────
// Initialization
// ──────────────────────────────────────────────────────────────────────────────

void initSensors() {
  // Configure ESP32 ADC attenuation for the 0-3.3V measurement range
  analogSetAttenuation(ADC_11db);
  sensors.begin();
}

void loadCalibration() {
  preferences.begin("ph_cal", true);  // Read-only mode
  calV4 = preferences.getFloat("v4", 3.1934f);
  calV7 = preferences.getFloat("v7", 2.6890f);
  preferences.end();

  preferences.begin("turb_cal", true);
  calTurbV0 = preferences.getFloat("v0", 3.3f);
  calTurbV400 = preferences.getFloat("v400", 1.0f);
  preferences.end();

  Serial.println("═══════════════════════════════════════");
  Serial.println("  Sensor Calibration Loaded from NVS");
  Serial.println("  -- pH (2-Point) --");
  Serial.printf("  V(pH 4.01) = %.4f V\n", calV4);
  Serial.printf("  V(pH 6.86) = %.4f V\n", calV7);
  Serial.println("  -- Turbidity (2-Point) --");
  Serial.printf("  V(0 NTU)   = %.4f V\n", calTurbV0);
  Serial.printf("  V(400 NTU) = %.4f V\n", calTurbV400);
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

float readTurbidityNTU() {
  float voltage = readTrimmedMeanVoltage(TURB_PIN);
  
  if (abs(calTurbV0 - calTurbV400) < 0.001f) return 0.0f;

  float m = (TURB_REF_400 - TURB_REF_0) / (calTurbV400 - calTurbV0);
  float b = TURB_REF_400 - (m * calTurbV400);
  
  float ntu = (m * voltage) + b;
  
  if (ntu < 0.0f) ntu = 0.0f;
  if (ntu > 3000.0f) ntu = 3000.0f;
  
  return ntu;
}

String getTurbidityStatus(float ntuValue) {
  // Qualitative thresholds for water clarity based on NTU
  if (ntuValue < 25.0f) return "Jernih";
  else if (ntuValue < 100.0f) return "Keruh";
  else return "Kotor";
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

  // ── CALINFO: Display current calibration status ──
  } else if (cmd == "CALINFO") {
    Serial.println("\n═══════════════════════════════════════");
    Serial.println("  STATUS KALIBRASI SENSOR");
    Serial.println("═══════════════════════════════════════");
    Serial.println("  -- pH (2-Point) --");
    Serial.printf("  V(pH4.01) = %.4f V\n", calV4);
    Serial.printf("  V(pH6.86) = %.4f V\n", calV7);
    Serial.printf("  Sesi aktif: CAL4=%s, CAL7=%s\n",
                  cal4Set ? "✓" : "—", cal7Set ? "✓" : "—");
    if (cal4Set) Serial.printf("  V(pH4_temp) = %.4f V\n", cal4Voltage);
    if (cal7Set) Serial.printf("  V(pH7_temp) = %.4f V\n", cal7Voltage);
    Serial.println("  -- Turbidity (2-Point) --");
    Serial.printf("  V(0 NTU)   = %.4f V\n", calTurbV0);
    Serial.printf("  V(400 NTU) = %.4f V\n", calTurbV400);
    Serial.printf("  Sesi aktif: TCAL0=%s, TCAL400=%s\n",
                  calTurb0Set ? "✓" : "—", calTurb400Set ? "✓" : "—");
    if (calTurb0Set) Serial.printf("  V(0_temp)   = %.4f V\n", calTurb0Voltage);
    if (calTurb400Set) Serial.printf("  V(400_temp) = %.4f V\n", calTurb400Voltage);
    Serial.println("═══════════════════════════════════════");
    Serial.println("  Perintah: CAL4 | CAL7 | CALSAVE | TCAL0 | TCAL400 | TCALSAVE | CALINFO");
    Serial.println("═══════════════════════════════════════");

  // ── TCAL0: Record voltage at 0 NTU ──
  } else if (cmd == "TCAL0") {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║  KALIBRASI TURBIDITY 0 NTU Sampling  ║");
    Serial.println("╚══════════════════════════════════════╝");
    calTurb0Voltage = readTrimmedMeanVoltage(TURB_PIN);
    calTurb0Set = true;
    Serial.printf("  ✓ Tegangan tercatat: %.4f V\n", calTurb0Voltage);
    Serial.printf("  ✓ ADC ~ %d\n", (int)(calTurb0Voltage / 3.3f * 4095.0f));
    Serial.println("  → Lanjutkan: celupkan ke air 400 NTU, lalu ketik TCAL400");

  // ── TCAL400: Record voltage at 400 NTU ──
  } else if (cmd == "TCAL400") {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║ KALIBRASI TURBIDITY 400 NTU Sampling ║");
    Serial.println("╚══════════════════════════════════════╝");
    calTurb400Voltage = readTrimmedMeanVoltage(TURB_PIN);
    calTurb400Set = true;
    Serial.printf("  ✓ Tegangan tercatat: %.4f V\n", calTurb400Voltage);
    Serial.printf("  ✓ ADC ~ %d\n", (int)(calTurb400Voltage / 3.3f * 4095.0f));
    Serial.println("  → Lanjutkan: ketik TCALSAVE");

  // ── TCALSAVE: Persist Turbidity to NVS ──
  } else if (cmd == "TCALSAVE") {
    if (!calTurb0Set || !calTurb400Set) {
      Serial.println("\n  ✗ ERROR: Kedua titik kalibrasi (0, 400) harus diambil terlebih dahulu!");
      Serial.println("    Jalankan TCAL0 dan TCAL400 sebelum TCALSAVE.");
      return;
    }
    if (abs(calTurb0Voltage - calTurb400Voltage) < 0.001f) {
      Serial.println("\n  ✗ ERROR: Tegangan antar buffer terlalu mirip!");
      return;
    }
    
    preferences.begin("turb_cal", false);
    preferences.putFloat("v0", calTurb0Voltage);
    preferences.putFloat("v400", calTurb400Voltage);
    preferences.end();

    calTurbV0 = calTurb0Voltage;
    calTurbV400 = calTurb400Voltage;
    calTurb0Set = false;
    calTurb400Set = false;

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║    ✓ KALIBRASI TURBIDITY DISIMPAN    ║");
    Serial.println("╠══════════════════════════════════════╣");
    Serial.printf("║  V(0 NTU)   = %.4f V                ║\n", calTurbV0);
    Serial.printf("║  V(400 NTU) = %.4f V                ║\n", calTurbV400);
    Serial.println("╚══════════════════════════════════════╝");
  }
}