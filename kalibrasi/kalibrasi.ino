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

float readVoltage() {
  int raw[ADC_SAMPLES];
  for (int i = 0; i < ADC_SAMPLES; i++) {
    raw[i] = analogRead(PH_PIN);
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
float readVoltageCalibration(unsigned long durationMs) {
  const int MAX_CAL = 500;
  int valid[MAX_CAL];
  int validCount = 0;
  unsigned long start = millis();

  while (millis() - start < durationMs && validCount < MAX_CAL) {
    int raw = analogRead(PH_PIN);
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

    cal4Voltage = readVoltageCalibration(10000);
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

    cal7Voltage = readVoltageCalibration(10000);
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

  } else if (cmd == "CALINFO") {
    Serial.println("\n═══════════════════════════════════════");
    Serial.println("  STATUS KALIBRASI pH (2-POINT)");
    Serial.println("═══════════════════════════════════════");
    Serial.printf("  V(pH4.01) = %.4f V\n", calV4);
    Serial.printf("  V(pH6.86) = %.4f V\n", calV7);
    Serial.printf("  Sesi: CAL4=%s CAL7=%s\n",
                  cal4Set?"✓":"—", cal7Set?"✓":"—");
    
    char buf[17];
    lcd.clear();
    snprintf(buf, sizeof(buf), "4:%.2f 7:%.2f", calV4, calV7);
    lcd.setCursor(0, 0); lcd.print(buf);
    lcd.setCursor(0, 1); lcd.print("2-Point Active");

  } else if (cmd == "READ") {
    continuousRead = !continuousRead;
    Serial.printf("[Read] %s\n", continuousRead ? "AKTIF" : "NONAKTIF");
    if (!continuousRead) lcdPrint("pH Cal Tool", "Ketik perintah..");

  } else if (cmd.length() > 0) {
    Serial.println("  Perintah tidak dikenal.");
    Serial.println("  Gunakan: CAL4 | CAL7 | CALSAVE | CALINFO | READ");
    lcdPrint("Cmd tdk dikenal", "CAL4/7/SAVE");
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

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║  SMART BUOY — pH CALIBRATION TOOL    ║");
  Serial.println("╠══════════════════════════════════════╣");
  Serial.printf("║  V(pH4.01) = %.4f V                 ║\n", calV4);
  Serial.printf("║  V(pH6.86) = %.4f V                 ║\n", calV7);
  Serial.println("╠══════════════════════════════════════╣");
  Serial.println("║  CAL4 | CAL7 | CALSAVE | CALINFO    ║");
  Serial.println("║  READ                                ║");
  Serial.println("╚══════════════════════════════════════╝\n");

  lcdPrint("pH Cal Tool", "Ready...");
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

    float volt = readVoltage();
    if (volt < 0) {
      if (smoothedVolt < 0) {
        // Belum pernah dapat sinyal valid sama sekali
        Serial.println("[pH] NO SIGNAL — cek koneksi probe");
        lcdPrint("NO SIGNAL", "Cek koneksi!");
      }
      // Jika sudah ada nilai sebelumnya, tampilkan nilai terakhir (jangan reset)
    } else {
      // EMA smoothing
      if (smoothedVolt < 0) smoothedVolt = volt;  // inisialisasi pertama kali
      smoothedVolt = EMA_ALPHA * volt + (1.0f - EMA_ALPHA) * smoothedVolt;

      float ph = computePH(smoothedVolt);
      Serial.printf("[pH] %.2f | V=%.4fV (raw=%.4fV)\n", ph, smoothedVolt, volt);
      char line1[17], line2[17];
      snprintf(line1, sizeof(line1), "pH : %.2f", ph);
      snprintf(line2, sizeof(line2), "V  : %.4f V", smoothedVolt);
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print(line1);
      lcd.setCursor(0, 1); lcd.print(line2);
    }
  }
}
