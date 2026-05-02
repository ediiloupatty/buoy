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
// Default values used when NVS is empty (first boot or after NVS erase)
float calM = -4.79f;   ///< Slope of the linear calibration equation
float calB = 18.06f;   ///< Y-intercept of the linear calibration equation

// Temporary calibration buffer (populated during interactive calibration session)
float cal4Voltage = 0.0f;  ///< Recorded voltage at pH 4.01 buffer
float cal7Voltage = 0.0f;  ///< Recorded voltage at pH 6.86 buffer
bool  cal4Set     = false;  ///< Flag: pH 4.01 point captured
bool  cal7Set     = false;  ///< Flag: pH 6.86 point captured

// Reference pH values for standard buffer solutions
const float PH_REF_4 = 4.01f;
const float PH_REF_7 = 6.86f;

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
  calM = preferences.getFloat("m", -4.79f);
  calB = preferences.getFloat("b", 18.06f);
  preferences.end();

  Serial.println("═══════════════════════════════════════");
  Serial.println("  pH Calibration Loaded from NVS");
  Serial.printf("  Slope (m) = %.4f\n", calM);
  Serial.printf("  Intercept (b) = %.4f\n", calB);
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
  return (calM * voltage) + calB;
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

int readTurbidityValue() {
  // Instantaneous single-shot ADC read
  return analogRead(TURB_PIN);
}

String getTurbidityStatus(int turbidityValue) {
  // Qualitative thresholds for water clarity
  if (turbidityValue < 1500) return "Jernih";
  else if (turbidityValue < 3000) return "Keruh";
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

  // ── CALSAVE: Calculate m/b and persist to NVS ──
  } else if (cmd == "CALSAVE") {
    if (!cal4Set || !cal7Set) {
      Serial.println("\n  ✗ ERROR: Kedua titik kalibrasi harus diambil terlebih dahulu!");
      Serial.println("    Jalankan CAL4 dan CAL7 sebelum CALSAVE.");
      return;
    }

    // Prevent division by zero (voltages must differ)
    float deltaV = cal7Voltage - cal4Voltage;
    if (abs(deltaV) < 0.001f) {
      Serial.println("\n  ✗ ERROR: Tegangan kedua buffer terlalu mirip!");
      Serial.println("    Pastikan sensor tercelup dengan benar di kedua larutan buffer.");
      return;
    }

    // Calculate linear calibration constants
    float newM = (PH_REF_7 - PH_REF_4) / deltaV;
    float newB = PH_REF_4 - (newM * cal4Voltage);

    // Persist to NVS
    preferences.begin("ph_cal", false);  // Read-write mode
    preferences.putFloat("m", newM);
    preferences.putFloat("b", newB);
    preferences.end();

    // Update runtime values
    calM = newM;
    calB = newB;

    // Reset calibration session
    cal4Set = false;
    cal7Set = false;

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║    ✓ KALIBRASI BERHASIL DISIMPAN     ║");
    Serial.println("╠══════════════════════════════════════╣");
    Serial.printf("║  V(pH4) = %.4f V                    ║\n", cal4Voltage);
    Serial.printf("║  V(pH7) = %.4f V                    ║\n", cal7Voltage);
    Serial.printf("║  Slope (m)     = %.4f              ║\n", calM);
    Serial.printf("║  Intercept (b) = %.4f              ║\n", calB);
    Serial.println("╠══════════════════════════════════════╣");
    Serial.println("║  Data tersimpan di NVS Flash.        ║");
    Serial.println("║  Restart aman — nilai tidak hilang.  ║");
    Serial.println("╚══════════════════════════════════════╝");

  // ── CALINFO: Display current calibration status ──
  } else if (cmd == "CALINFO") {
    Serial.println("\n═══════════════════════════════════════");
    Serial.println("  STATUS KALIBRASI pH");
    Serial.println("═══════════════════════════════════════");
    Serial.printf("  Slope (m)     = %.4f\n", calM);
    Serial.printf("  Intercept (b) = %.4f\n", calB);
    Serial.printf("  Sesi aktif    : CAL4=%s, CAL7=%s\n",
                  cal4Set ? "✓" : "—", cal7Set ? "✓" : "—");
    if (cal4Set) Serial.printf("  V(pH4) = %.4f V\n", cal4Voltage);
    if (cal7Set) Serial.printf("  V(pH7) = %.4f V\n", cal7Voltage);
    Serial.println("═══════════════════════════════════════");
    Serial.println("  Perintah: CAL4 | CAL7 | CALSAVE | CALINFO");
    Serial.println("═══════════════════════════════════════");
  }
}