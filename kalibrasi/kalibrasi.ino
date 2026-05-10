/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Edi Loupatty
 *
 * This file is part of the Smart Buoy IoT System.
 */

/**
 * @file kalibrasi.ino
 * @brief Standalone pH Sensor Calibration Tool.
 * 
 * Program terpisah dari firmware utama, digunakan HANYA untuk kalibrasi
 * sensor pH. Upload sketch ini ke ESP32 saat perlu kalibrasi, kemudian
 * upload kembali firmware utama (final_project.ino) setelah selesai.
 * 
 * Hasil kalibrasi tersimpan di NVS flash dan akan tetap terbaca oleh
 * firmware utama karena menggunakan namespace NVS yang sama ("ph_cal").
 * 
 * Perintah Serial:
 *   CAL4    — Catat tegangan di buffer pH 4.01
 *   CAL7    — Catat tegangan di buffer pH 6.86
 *   CALSAVE — Hitung & simpan kalibrasi ke NVS
 *   CALINFO — Tampilkan status kalibrasi
 *   READ    — Baca sensor pH, suhu, dan turbidity secara real-time
 * 
 * Panduan lengkap: lihat KALIBRASI.md
 */

#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>

// ── Pin Definitions (harus sama dengan Config.h) ────────────────────────────
#define PH_PIN   34
#define TURB_PIN 35
#define TEMP_PIN 4

// ── DS18B20 Temperature Sensor ──────────────────────────────────────────────
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);

// ── NVS Storage ─────────────────────────────────────────────────────────────
Preferences preferences;

// ── pH Calibration State ────────────────────────────────────────────────────
float calV4 = 3.03f;
float calV7 = 2.50f;
float calV9 = 2.08f;

float cal4Voltage = 0.0f;
float cal7Voltage = 0.0f;
float cal9Voltage = 0.0f;
bool  cal4Set     = false;
bool  cal7Set     = false;
bool  cal9Set     = false;

const float PH_REF_4 = 4.01f;
const float PH_REF_7 = 6.86f;
const float PH_REF_9 = 9.18f;

// ── ADC Configuration ───────────────────────────────────────────────────────
const int ADC_SAMPLES        = 10;
const int ADC_TRIM_COUNT     = 2;
const int ADC_SAMPLE_DELAY   = 10;

// ── Timing ──────────────────────────────────────────────────────────────────
unsigned long lastReadMillis = 0;
const unsigned long READ_INTERVAL = 2000;  // Baca sensor setiap 2 detik
bool continuousRead = false;

// =============================================================================
// ADC Signal Processing
// =============================================================================

float readTrimmedMeanVoltage(int pin) {
  int samples[ADC_SAMPLES];

  for (int i = 0; i < ADC_SAMPLES; i++) {
    samples[i] = analogRead(pin);
    delay(ADC_SAMPLE_DELAY);
  }

  // Insertion sort
  for (int i = 1; i < ADC_SAMPLES; i++) {
    int key = samples[i];
    int j = i - 1;
    while (j >= 0 && samples[j] > key) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = key;
  }

  // Trimmed mean
  long sum = 0;
  int validCount = ADC_SAMPLES - (2 * ADC_TRIM_COUNT);
  for (int i = ADC_TRIM_COUNT; i < ADC_SAMPLES - ADC_TRIM_COUNT; i++) {
    sum += samples[i];
  }

  float avgAdc = (float)sum / validCount;
  return avgAdc * (3.3f / 4095.0f);
}

// =============================================================================
// Sensor Reading
// =============================================================================

float readTemperature() {
  tempSensor.requestTemperatures();
  float t = tempSensor.getTempCByIndex(0);
  return (t <= -126.0f) ? -127.0f : t;
}

float readPH(float tempC) {
  float voltage = readTrimmedMeanVoltage(PH_PIN);
  float m, b;
  
  if (voltage > calV7) {
    m = (PH_REF_7 - PH_REF_4) / (calV7 - calV4);
    b = PH_REF_7 - (m * calV7);
  } else {
    m = (PH_REF_9 - PH_REF_7) / (calV9 - calV7);
    b = PH_REF_7 - (m * calV7);
  }
  
  float rawPH = (m * voltage) + b;

  // Nernst temperature compensation
  if (tempC > -126.0f) {
    rawPH += 0.003f * (tempC - 25.0f);
  }
  return rawPH;
}

// =============================================================================
// Calibration Commands
// =============================================================================

void handleCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "CAL4") {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║   KALIBRASI pH 4.01 — Sampling...    ║");
    Serial.println("╚══════════════════════════════════════╝");

    cal4Voltage = readTrimmedMeanVoltage(PH_PIN);
    cal4Set = true;

    Serial.printf("  ✓ Tegangan tercatat: %.4f V\n", cal4Voltage);
    Serial.printf("  ✓ ADC ~ %d\n", (int)(cal4Voltage / 3.3f * 4095.0f));
    Serial.println("  → Lanjutkan: celupkan ke buffer pH 6.86, lalu ketik CAL7");

  } else if (cmd == "CAL7") {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║   KALIBRASI pH 6.86 — Sampling...    ║");
    Serial.println("╚══════════════════════════════════════╝");

    cal7Voltage = readTrimmedMeanVoltage(PH_PIN);
    cal7Set = true;

    Serial.printf("  ✓ Tegangan tercatat: %.4f V\n", cal7Voltage);
    Serial.printf("  ✓ ADC ~ %d\n", (int)(cal7Voltage / 3.3f * 4095.0f));
    Serial.println("  → Lanjutkan: ketik CALSAVE untuk menghitung & menyimpan kalibrasi");

  } else if (cmd == "CAL9") {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║   KALIBRASI pH 9.18 — Sampling...    ║");
    Serial.println("╚══════════════════════════════════════╝");

    cal9Voltage = readTrimmedMeanVoltage(PH_PIN);
    cal9Set = true;

    Serial.printf("  ✓ Tegangan tercatat: %.4f V\n", cal9Voltage);
    Serial.printf("  ✓ ADC ~ %d\n", (int)(cal9Voltage / 3.3f * 4095.0f));
    Serial.println("  → Lanjutkan: ketik CALSAVE untuk menyimpan kalibrasi 3-titik");

  } else if (cmd == "CALSAVE") {
    if (!cal4Set || !cal7Set || !cal9Set) {
      Serial.println("\n  ✗ ERROR: Ketiga titik kalibrasi (4.01, 6.86, 9.18) harus diambil terlebih dahulu!");
      Serial.println("    Jalankan CAL4, CAL7, dan CAL9 sebelum CALSAVE.");
      return;
    }

    if (abs(cal7Voltage - cal4Voltage) < 0.001f || abs(cal9Voltage - cal7Voltage) < 0.001f) {
      Serial.println("\n  ✗ ERROR: Tegangan antar buffer terlalu mirip!");
      return;
    }

    preferences.begin("ph_cal", false);
    preferences.putFloat("v4", cal4Voltage);
    preferences.putFloat("v7", cal7Voltage);
    preferences.putFloat("v9", cal9Voltage);
    preferences.end();

    calV4 = cal4Voltage;
    calV7 = cal7Voltage;
    calV9 = cal9Voltage;
    cal4Set = false;
    cal7Set = false;
    cal9Set = false;

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║    ✓ KALIBRASI BERHASIL DISIMPAN     ║");
    Serial.println("╠══════════════════════════════════════╣");
    Serial.printf("║  V(pH4) = %.4f V                    ║\n", calV4);
    Serial.printf("║  V(pH7) = %.4f V                    ║\n", calV7);
    Serial.printf("║  V(pH9) = %.4f V                    ║\n", calV9);
    Serial.println("╠══════════════════════════════════════╣");
    Serial.println("║  Data tersimpan di NVS Flash.        ║");
    Serial.println("║  Upload firmware utama — nilai       ║");
    Serial.println("║  kalibrasi akan terbaca otomatis.    ║");
    Serial.println("╚══════════════════════════════════════╝");

  } else if (cmd == "CALINFO") {
    Serial.println("\n═══════════════════════════════════════");
    Serial.println("  STATUS KALIBRASI pH (3-Point)");
    Serial.println("═══════════════════════════════════════");
    Serial.printf("  V(pH4) = %.4f V\n", calV4);
    Serial.printf("  V(pH7) = %.4f V\n", calV7);
    Serial.printf("  V(pH9) = %.4f V\n", calV9);
    Serial.printf("  Sesi aktif    : CAL4=%s, CAL7=%s, CAL9=%s\n",
                  cal4Set ? "✓" : "—", cal7Set ? "✓" : "—", cal9Set ? "✓" : "—");
    if (cal4Set) Serial.printf("  V(pH4_temp) = %.4f V\n", cal4Voltage);
    if (cal7Set) Serial.printf("  V(pH7_temp) = %.4f V\n", cal7Voltage);
    if (cal9Set) Serial.printf("  V(pH9_temp) = %.4f V\n", cal9Voltage);
    Serial.println("═══════════════════════════════════════");

  } else if (cmd == "READ") {
    continuousRead = !continuousRead;
    Serial.printf("[Read] Pembacaan kontinu: %s\n", continuousRead ? "AKTIF" : "NONAKTIF");

  } else if (cmd.length() > 0) {
    Serial.println("\n  Perintah tidak dikenal.");
    Serial.println("  Gunakan: CAL4 | CAL7 | CAL9 | CALSAVE | CALINFO | READ");
  }
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  analogSetAttenuation(ADC_11db);
  tempSensor.begin();

  // Load existing calibration from NVS
  preferences.begin("ph_cal", true);
  calV4 = preferences.getFloat("v4", 3.03f);
  calV7 = preferences.getFloat("v7", 2.50f);
  calV9 = preferences.getFloat("v9", 2.08f);
  preferences.end();

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║  SMART BUOY — pH CALIBRATION TOOL    ║");
  Serial.println("╠══════════════════════════════════════╣");
  Serial.printf("║  V(pH4) = %.4f V                    ║\n", calV4);
  Serial.printf("║  V(pH7) = %.4f V                    ║\n", calV7);
  Serial.printf("║  V(pH9) = %.4f V                    ║\n", calV9);
  Serial.println("╠══════════════════════════════════════╣");
  Serial.println("║  Perintah:                           ║");
  Serial.println("║    CAL4    — Kalibrasi buffer pH 4   ║");
  Serial.println("║    CAL7    — Kalibrasi buffer pH 7   ║");
  Serial.println("║    CAL9    — Kalibrasi buffer pH 9   ║");
  Serial.println("║    CALSAVE — Simpan kalibrasi        ║");
  Serial.println("║    CALINFO — Status kalibrasi        ║");
  Serial.println("║    READ    — Toggle baca sensor      ║");
  Serial.println("╚══════════════════════════════════════╝\n");
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
  // Handle serial commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    handleCommand(cmd);
  }

  // Continuous sensor reading (when enabled via READ command)
  if (continuousRead && millis() - lastReadMillis >= READ_INTERVAL) {
    lastReadMillis = millis();

    float temp = readTemperature();
    float ph   = readPH(temp);
    int   turb = analogRead(TURB_PIN);
    float volt = readTrimmedMeanVoltage(PH_PIN);

    Serial.printf("[Sensor] Suhu=%.1f°C | pH=%.2f | Turb=%d | V_pH=%.4fV\n",
                  temp, ph, turb, volt);
  }
}
