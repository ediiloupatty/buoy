/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Edi Loupatty
 *
 * This file is part of the Smart Buoy IoT System.
 */

/**
 * @file kalibrasi.ino
 * @brief Standalone pH Sensor Calibration Tool with LCD I2C 16x2.
 * 
 * Perintah Serial:
 *   CAL4    — Catat tegangan di buffer pH 4.01
 *   CAL7    — Catat tegangan di buffer pH 6.86
 *   CALSAVE — Simpan kalibrasi ke NVS
 *   CALINFO — Tampilkan status kalibrasi
 *   READ    — Toggle baca sensor real-time
 */

#include <Preferences.h>
#include <LiquidCrystal_I2C.h>

// ── LCD I2C (SDA=21, SCL=22) ─────────────────────────────────────────────────
// Ganti 0x27 ke 0x3F jika LCD tidak muncul
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ── Pin ──────────────────────────────────────────────────────────────────────
#define PH_PIN 34
#define TURB_PIN 35

// ── NVS ──────────────────────────────────────────────────────────────────────
Preferences preferences;

// ── Calibration State ────────────────────────────────────────────────────────
float calV4 = 3.1228f;
float calV7 = 2.0752f;

float cal4Voltage = 0.0f;
float cal7Voltage = 0.0f;
bool  cal4Set     = false;
bool  cal7Set     = false;

const float PH_REF_4 = 4.01f;
const float PH_REF_7 = 6.86f;

// ── Turbidity Calibration State ───────────────────────────────────────────────
float calTurbV0 = 3.3f;
float calTurbV400 = 1.0f;

float calTurb0Voltage = 0.0f;
float calTurb400Voltage = 0.0f;
bool  calTurb0Set     = false;
bool  calTurb400Set   = false;

const float TURB_REF_0   = 0.0f;
const float TURB_REF_400 = 400.0f;

// ── ADC ──────────────────────────────────────────────────────────────────────
const int ADC_SAMPLES      = 50;
const int ADC_TRIM_COUNT   = 5;
const int ADC_SAMPLE_DELAY = 10;
const int ADC_FLOAT_LOW    = 2000;
const int ADC_FLOAT_HIGH   = 2100;

// ── Timing ───────────────────────────────────────────────────────────────────
unsigned long lastReadMillis = 0;
const unsigned long READ_INTERVAL = 1000;
bool continuousRead = true;

// ── EMA Smoothing ─────────────────────────────────────────────────────────────
const float EMA_ALPHA = 0.2f;   // 0.1=sangat halus, 0.5=lebih responsif
float smoothedVolt    = -1.0f;  // -1 = belum ada pembacaan valid
float smoothedTurbVolt = -1.0f;

// =============================================================================
// LCD Helpers
// =============================================================================

void lcdPrint(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);
}

void lcdStatus(const char* line1, float voltage) {
  char buf[17];
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1);
  snprintf(buf, sizeof(buf), "V=%.4fV OK", voltage);
  lcd.print(buf);
}

// =============================================================================
// ADC — filter floating midpoint (~1.65V / ADC 2047)
// Kembalikan -1.0 jika semua sampel floating
// =============================================================================

float readVoltage(int pin) {
  int raw[ADC_SAMPLES];
  for (int i = 0; i < ADC_SAMPLES; i++) {
    raw[i] = analogRead(pin);
    delay(ADC_SAMPLE_DELAY);
  }

  int valid[ADC_SAMPLES];
  int validCount = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    if (raw[i] < ADC_FLOAT_LOW || raw[i] > ADC_FLOAT_HIGH)
      valid[validCount++] = raw[i];
  }
  if (validCount < 4) return -1.0f;

  for (int i = 1; i < validCount; i++) {
    int key = valid[i], j = i - 1;
    while (j >= 0 && valid[j] > key) { valid[j + 1] = valid[j]; j--; }
    valid[j + 1] = key;
  }

  int trim = min(ADC_TRIM_COUNT, validCount / 4);
  long sum = 0; int used = 0;
  for (int i = trim; i < validCount - trim; i++) { sum += valid[i]; used++; }
  if (used == 0) return -1.0f;

  float result = ((float)sum / used) * (3.3f / 4095.0f);
  int resultADC = (int)(result / 3.3f * 4095.0f);
  if (resultADC >= ADC_FLOAT_LOW && resultADC <= ADC_FLOAT_HIGH) return -1.0f;
  return result;
}

// Sampling lambat untuk kalibrasi — kumpulkan sampel valid selama durasi ms
// Lebih toleran terhadap koneksi intermittent
float readVoltageCalibration(int pin, unsigned long durationMs) {
  const int MAX_CAL = 500;
  int valid[MAX_CAL];
  int validCount = 0;
  unsigned long start = millis();

  while (millis() - start < durationMs && validCount < MAX_CAL) {
    int raw = analogRead(pin);
    if (raw < ADC_FLOAT_LOW || raw > ADC_FLOAT_HIGH)
      valid[validCount++] = raw;
    delay(20);
  }

  Serial.printf("  [CAL] %d sampel valid dari %lu ms sampling\n",
                validCount, durationMs);

  if (validCount < 10) return -1.0f;

  // Sort & trimmed mean
  for (int i = 1; i < validCount; i++) {
    int key = valid[i], j = i - 1;
    while (j >= 0 && valid[j] > key) { valid[j + 1] = valid[j]; j--; }
    valid[j + 1] = key;
  }
  int trim = validCount / 8;
  long sum = 0; int used = 0;
  for (int i = trim; i < validCount - trim; i++) { sum += valid[i]; used++; }
  if (used == 0) return -1.0f;

  float result = ((float)sum / used) * (3.3f / 4095.0f);
  int resultADC = (int)(result / 3.3f * 4095.0f);
  if (resultADC >= ADC_FLOAT_LOW && resultADC <= ADC_FLOAT_HIGH) return -1.0f;
  return result;
}

// =============================================================================
// pH Calculation — Two-Point Linear Calibration
// =============================================================================

float computePH(float voltage) {
  // Cegah pembagian dengan nol
  if (abs(calV7 - calV4) < 0.001f) return 7.0f;

  // Rumus Linear: y = mx + b
  float m = (PH_REF_7 - PH_REF_4) / (calV7 - calV4);
  float b = PH_REF_7 - (m * calV7);

  float ph = (m * voltage) + b;
  
  // Clamp ke range pH yang masuk akal
  if (ph < 0.0f)  ph = 0.0f;
  if (ph > 14.0f) ph = 14.0f;
  return ph;
}

float computeNTU(float voltage) {
  if (abs(calTurbV0 - calTurbV400) < 0.001f) return 0.0f;

  float m = (TURB_REF_400 - TURB_REF_0) / (calTurbV400 - calTurbV0);
  float b = TURB_REF_400 - (m * calTurbV400);

  float ntu = (m * voltage) + b;
  
  if (ntu < 0.0f)  ntu = 0.0f;
  if (ntu > 3000.0f) ntu = 3000.0f;
  return ntu;
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
    lcdPrint("CAL4 pH4.01", "Sampling...");

    cal4Voltage = readVoltageCalibration(PH_PIN, 10000);
    if (cal4Voltage < 0) {
      Serial.println("  ✗ ERROR: Sinyal floating. Cek koneksi probe!");
      lcdPrint("CAL4 GAGAL", "Cek koneksi!"); return;
    }
    cal4Set = true;
    Serial.printf("  ✓ Tegangan: %.4f V (ADC ~%d)\n", cal4Voltage, (int)(cal4Voltage / 3.3f * 4095.0f));
    Serial.println("  → Lanjut: celupkan ke pH 6.86, ketik CAL7");
    lcdStatus("CAL4 OK", cal4Voltage);

  } else if (cmd == "CAL7") {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║   KALIBRASI pH 6.86 — Sampling...    ║");
    Serial.println("╚══════════════════════════════════════╝");
    lcdPrint("CAL7 pH6.86", "Sampling...");

    cal7Voltage = readVoltageCalibration(PH_PIN, 10000);
    if (cal7Voltage < 0) {
      Serial.println("  ✗ ERROR: Sinyal floating. Cek koneksi probe!");
      lcdPrint("CAL7 GAGAL", "Cek koneksi!"); return;
    }
    cal7Set = true;
    Serial.printf("  ✓ Tegangan: %.4f V (ADC ~%d)\n", cal7Voltage, (int)(cal7Voltage / 3.3f * 4095.0f));
    Serial.println("  → Lanjut: ketik CALSAVE");
    lcdStatus("CAL7 OK", cal7Voltage);

  } else if (cmd == "CALSAVE") {
    if (!cal4Set || !cal7Set) {
      Serial.println("\n  ✗ Jalankan CAL4 dan CAL7 terlebih dahulu!");
      lcdPrint("ERROR CALSAVE", "Ambil CAL4 & CAL7"); return;
    }
    if (abs(cal7Voltage - cal4Voltage) < 0.001f) {
      Serial.println("\n  ✗ Tegangan antar buffer terlalu mirip!");
      lcdPrint("ERROR: V mirip", "Ulangi kalibrasi"); return;
    }

    preferences.begin("ph_cal", false);
    preferences.putFloat("v4", cal4Voltage);
    preferences.putFloat("v7", cal7Voltage);
    preferences.end();

    calV4 = cal4Voltage; calV7 = cal7Voltage;
    cal4Set = false; cal7Set = false;

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║    ✓ KALIBRASI BERHASIL DISIMPAN     ║");
    Serial.println("╠══════════════════════════════════════╣");
    Serial.printf("║  V(pH4.01) = %.4f V                 ║\n", calV4);
    Serial.printf("║  V(pH6.86) = %.4f V                 ║\n", calV7);
    Serial.println("╚══════════════════════════════════════╝");
    lcdPrint("SAVED ke NVS!", "Upload FW utama");

  } else if (cmd == "TCAL0") {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║  KALIBRASI TURBIDITY 0 NTU Sampling  ║");
    Serial.println("╚══════════════════════════════════════╝");
    lcdPrint("TCAL0 0 NTU", "Sampling...");

    calTurb0Voltage = readVoltageCalibration(TURB_PIN, 10000);
    if (calTurb0Voltage < 0) {
      Serial.println("  ✗ ERROR: Sinyal floating. Cek koneksi probe!");
      lcdPrint("TCAL0 GAGAL", "Cek koneksi!"); return;
    }
    calTurb0Set = true;
    Serial.printf("  ✓ Tegangan: %.4f V (ADC ~%d)\n", calTurb0Voltage, (int)(calTurb0Voltage / 3.3f * 4095.0f));
    Serial.println("  → Lanjut: celupkan ke air 400 NTU, ketik TCAL400");
    lcdStatus("TCAL0 OK", calTurb0Voltage);

  } else if (cmd == "TCAL400") {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║ KALIBRASI TURBIDITY 400 NTU Sampling ║");
    Serial.println("╚══════════════════════════════════════╝");
    lcdPrint("TCAL400 400 NTU", "Sampling...");

    calTurb400Voltage = readVoltageCalibration(TURB_PIN, 10000);
    if (calTurb400Voltage < 0) {
      Serial.println("  ✗ ERROR: Sinyal floating. Cek koneksi probe!");
      lcdPrint("TCAL400 GAGAL", "Cek koneksi!"); return;
    }
    calTurb400Set = true;
    Serial.printf("  ✓ Tegangan: %.4f V (ADC ~%d)\n", calTurb400Voltage, (int)(calTurb400Voltage / 3.3f * 4095.0f));
    Serial.println("  → Lanjut: ketik TCALSAVE");
    lcdStatus("TCAL400 OK", calTurb400Voltage);

  } else if (cmd == "TCALSAVE") {
    if (!calTurb0Set || !calTurb400Set) {
      Serial.println("\n  ✗ Jalankan TCAL0 dan TCAL400 terlebih dahulu!");
      lcdPrint("ERROR TCALSAVE", "Ambil TCAL0&400"); return;
    }
    if (abs(calTurb0Voltage - calTurb400Voltage) < 0.001f) {
      Serial.println("\n  ✗ Tegangan antar buffer terlalu mirip!");
      lcdPrint("ERROR: V mirip", "Ulangi kalibrasi"); return;
    }

    preferences.begin("turb_cal", false);
    preferences.putFloat("v0", calTurb0Voltage);
    preferences.putFloat("v400", calTurb400Voltage);
    preferences.end();

    calTurbV0 = calTurb0Voltage; calTurbV400 = calTurb400Voltage;
    calTurb0Set = false; calTurb400Set = false;

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║    ✓ KALIBRASI TURBIDITY DISIMPAN    ║");
    Serial.println("╠══════════════════════════════════════╣");
    Serial.printf("║  V(0 NTU)   = %.4f V                ║\n", calTurbV0);
    Serial.printf("║  V(400 NTU) = %.4f V                ║\n", calTurbV400);
    Serial.println("╚══════════════════════════════════════╝");
    lcdPrint("TURB SAVED!", "Upload FW utama");

  } else if (cmd == "CALINFO") {
    Serial.println("\n═══════════════════════════════════════");
    Serial.println("  STATUS KALIBRASI SENSOR (2-POINT)");
    Serial.println("═══════════════════════════════════════");
    Serial.println("  -- pH --");
    Serial.printf("  V(pH4.01) = %.4f V\n", calV4);
    Serial.printf("  V(pH6.86) = %.4f V\n", calV7);
    Serial.printf("  Sesi: CAL4=%s CAL7=%s\n",
                  cal4Set?"✓":"—", cal7Set?"✓":"—");
    Serial.println("  -- Turbidity --");
    Serial.printf("  V(0 NTU)   = %.4f V\n", calTurbV0);
    Serial.printf("  V(400 NTU) = %.4f V\n", calTurbV400);
    Serial.printf("  Sesi: TCAL0=%s TCAL400=%s\n",
                  calTurb0Set?"✓":"—", calTurb400Set?"✓":"—");
    
    char buf[17];
    lcd.clear();
    snprintf(buf, sizeof(buf), "p4:%.1f p7:%.1f", calV4, calV7);
    lcd.setCursor(0, 0); lcd.print(buf);
    snprintf(buf, sizeof(buf), "t0:%.1f tX:%.1f", calTurbV0, calTurbV400);
    lcd.setCursor(0, 1); lcd.print(buf);

  } else if (cmd == "READ") {
    continuousRead = !continuousRead;
    Serial.printf("[Read] %s\n", continuousRead ? "AKTIF" : "NONAKTIF");
    if (!continuousRead) lcdPrint("pH Cal Tool", "Ketik perintah..");

  } else if (cmd.length() > 0) {
    Serial.println("  Perintah tidak dikenal.");
    Serial.println("  Gunakan: CAL4 | CAL7 | CALSAVE | TCAL0 | TCAL400 | TCALSAVE | CALINFO | READ");
    lcdPrint("Cmd tdk dikenal", "CAL/TCAL/INFO");
  }
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(100);

  lcd.init();
  lcd.backlight();
  lcdPrint("pH Cal Tool", "Memuat...");

  analogSetAttenuation(ADC_11db);

  preferences.begin("ph_cal", true);
  calV4 = preferences.getFloat("v4", 3.1228f);
  calV7 = preferences.getFloat("v7", 2.0752f);
  preferences.end();

  preferences.begin("turb_cal", true);
  calTurbV0 = preferences.getFloat("v0", 3.3f);
  calTurbV400 = preferences.getFloat("v400", 1.0f);
  preferences.end();

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║   SMART BUOY — CALIBRATION TOOL      ║");
  Serial.println("╠══════════════════════════════════════╣");
  Serial.printf("║  V(pH4.01) = %.4f V                 ║\n", calV4);
  Serial.printf("║  V(pH6.86) = %.4f V                 ║\n", calV7);
  Serial.printf("║  V(0 NTU)  = %.4f V                 ║\n", calTurbV0);
  Serial.printf("║  V(400NTU) = %.4f V                 ║\n", calTurbV400);
  Serial.println("╠══════════════════════════════════════╣");
  Serial.println("║  CAL4  | CAL7    | CALSAVE           ║");
  Serial.println("║  TCAL0 | TCAL400 | TCALSAVE          ║");
  Serial.println("║  READ  | CALINFO                     ║");
  Serial.println("╚══════════════════════════════════════╝\n");

  lcdPrint("SmartBuoy Cal", "Ready...");
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    handleCommand(cmd);
  }

  if (continuousRead && millis() - lastReadMillis >= READ_INTERVAL) {
    lastReadMillis = millis();

    float voltPH = readVoltage(PH_PIN);
    float voltTurb = readVoltage(TURB_PIN);
    
    // Process pH
    if (voltPH < 0) {
      if (smoothedVolt < 0) {
        Serial.println("[pH] NO SIGNAL");
      }
    } else {
      if (smoothedVolt < 0) smoothedVolt = voltPH;
      smoothedVolt = EMA_ALPHA * voltPH + (1.0f - EMA_ALPHA) * smoothedVolt;
    }

    // Process Turbidity
    if (voltTurb < 0) {
      if (smoothedTurbVolt < 0) {
        Serial.println("[Turb] NO SIGNAL");
      }
    } else {
      if (smoothedTurbVolt < 0) smoothedTurbVolt = voltTurb;
      smoothedTurbVolt = EMA_ALPHA * voltTurb + (1.0f - EMA_ALPHA) * smoothedTurbVolt;
    }

    // Display
    if (smoothedVolt >= 0 || smoothedTurbVolt >= 0) {
      float ph = computePH(smoothedVolt);
      float ntu = computeNTU(smoothedTurbVolt);
      
      Serial.printf("[READ] pH: %.2f (%.2fV) | Turb: %.1f NTU (%.2fV)\n", 
                    ph, smoothedVolt, ntu, smoothedTurbVolt);
                    
      char line1[17], line2[17];
      snprintf(line1, sizeof(line1), "pH:%.2f %.2fV", ph, smoothedVolt);
      snprintf(line2, sizeof(line2), "TB:%.1f %.2fV", ntu, smoothedTurbVolt);
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print(line1);
      lcd.setCursor(0, 1); lcd.print(line2);
    } else {
      lcdPrint("Sensor Error", "Cek koneksi!");
    }
  }
}
