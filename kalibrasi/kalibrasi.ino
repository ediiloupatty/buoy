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
 *   CAL1 <pH> — Kalibrasi 1-titik tanpa buffer (slope lama dipertahankan)
 *   CALINFO — Tampilkan status kalibrasi
 *   READ    — Toggle baca sensor real-time
 *   DIAG    — Diagnosa stray voltage / ground loop pada probe pH
 *   DIAGTURB— Diagnosa stray voltage pada sensor turbidity
 */

#include <Preferences.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ── LCD I2C (SDA=21, SCL=22) ─────────────────────────────────────────────────
// Ganti 0x27 ke 0x3F jika LCD tidak muncul
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ── Pin ──────────────────────────────────────────────────────────────────────
#define PH_PIN   34
#define TURB_PIN 35
#define TEMP_PIN  4

// ── DS18B20 ──────────────────────────────────────────────────────────────────
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensors(&oneWire);

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

// ── Turbidity Calibration (2-Point HARDCODED) ────────────────────────────────
// Sinkron dengan final_project/Sensors.cpp.
// 3.30 V -> 0 NTU (jernih), 2.26 V -> 400 NTU (keruh).
const float TURB_V_CLEAR  = 3.30f;
const float TURB_V_TURBID = 2.26f;
const float TURB_NTU_MAX  = 400.0f;

// ── ADC ──────────────────────────────────────────────────────────────────────
const int ADC_SAMPLES      = 50;
const int ADC_TRIM_COUNT   = 5;
const int ADC_SAMPLE_DELAY = 10;
const int ADC_FLOAT_LOW    = 2000;
const int ADC_FLOAT_HIGH   = 2100;

// ── Error Test ───────────────────────────────────────────────────────────────
const int ERR_SAMPLES = 5;

// ── Timing ───────────────────────────────────────────────────────────────────
unsigned long lastReadMillis = 0;
const unsigned long READ_INTERVAL = 1000;
bool continuousRead = true;

// ── EMA Smoothing ─────────────────────────────────────────────────────────────
const float EMA_ALPHA = 0.1f;   // 0.1=sangat halus, 0.5=lebih responsif
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

// Linear 2-titik hardcoded:
//   (TURB_V_CLEAR, 0 NTU) -> (TURB_V_TURBID, 400 NTU)
// SEN0189: kekeruhan naik => voltase turun.
float computeNTU(float voltage) {
  float m = TURB_NTU_MAX / (TURB_V_TURBID - TURB_V_CLEAR);
  float ntu = m * (voltage - TURB_V_CLEAR);

  if (ntu < 0.0f)  ntu = 0.0f;
  if (ntu > 3000.0f) ntu = 3000.0f;
  return ntu;
}

// Inversi computePH: dari pH balik ke tegangan yang seharusnya.
// pH = m*v + b  →  v = (pH - b) / m
float voltageForPH(float ph) {
  if (abs(calV7 - calV4) < 0.001f) return -1.0f;
  float m = (PH_REF_7 - PH_REF_4) / (calV7 - calV4);
  float b = PH_REF_7 - (m * calV7);
  return (ph - b) / m;
}

// =============================================================================
// DIAGNOSA — Deteksi Stray Voltage / Ground Loop
// =============================================================================
// Mengambil sampel ADC MENTAH (tanpa filter floating, tanpa trimmed-mean, tanpa
// EMA) selama 8 detik, lalu melaporkan statistik: min, maks, rata-rata,
// peak-to-peak, dan standar deviasi.
//
// Tujuan: probe pH yang benar di gelas tapi ngaco di tambak biasanya kena
// tegangan nyasar dari kincir/pompa. Filter di readVoltage() menyembunyikan
// gejalanya — fungsi ini sengaja TIDAK memfilter supaya gejalanya kelihatan.
//
// Cara pakai: jalankan DIAG saat probe di gelas, catat hasilnya, lalu jalankan
// lagi saat probe di tambak. Bandingkan 'Rata-rata' (offset DC) dan
// 'Peak-to-peak / Std' (riak AC).
// =============================================================================

void runDiagnostic(int pin, const char* label, bool isPH) {
  const unsigned long DURATION_MS = 8000;
  const int           SAMPLE_GAP  = 5;
  const float         K           = 3.3f / 4095.0f;

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║   DIAGNOSA STRAY VOLTAGE — 8 detik   ║");
  Serial.println("╚══════════════════════════════════════╝");
  Serial.printf ("  Sensor: %s  | Sampel ADC MENTAH (tanpa filter)\n", label);
  lcdPrint("DIAGNOSA 8s...", label);

  long   n = 0;
  double sum = 0, sumSq = 0;
  int    minAdc = 4095, maxAdc = 0;
  long   floatCount = 0;

  unsigned long start    = millis();
  unsigned long lastTick = start;

  while (millis() - start < DURATION_MS) {
    int raw = analogRead(pin);
    n++;
    sum   += raw;
    sumSq += (double)raw * (double)raw;
    if (raw < minAdc) minAdc = raw;
    if (raw > maxAdc) maxAdc = raw;
    if (raw >= ADC_FLOAT_LOW && raw <= ADC_FLOAT_HIGH) floatCount++;

    // Snapshot tiap 1 detik supaya riak kelihatan langsung
    if (millis() - lastTick >= 1000) {
      lastTick += 1000;
      Serial.printf("  [t=%lus] ADC=%4d  V=%.3f\n",
                    (millis() - start) / 1000, raw, raw * K);
    }
    delay(SAMPLE_GAP);
  }

  if (n < 1) { Serial.println("  ✗ Tidak ada sampel."); return; }

  float meanAdc = (float)(sum / n);
  float varAdc  = (float)(sumSq / n - (double)meanAdc * (double)meanAdc);
  if (varAdc < 0) varAdc = 0;
  float stdAdc  = sqrt(varAdc);
  int   p2pAdc  = maxAdc - minAdc;

  float meanV = meanAdc * K;
  float minV  = minAdc  * K;
  float maxV  = maxAdc  * K;
  float stdV  = stdAdc  * K;
  float p2pV  = p2pAdc  * K;
  float floatPct = 100.0f * floatCount / n;

  Serial.println("\n═══════════════════════════════════════════════════");
  Serial.printf ("  HASIL DIAGNOSA — %s\n", label);
  Serial.println("═══════════════════════════════════════════════════");
  Serial.printf ("  Total sampel    : %ld dalam 8.0 s\n", n);
  Serial.printf ("  Sampel floating : %ld  (%.1f %%)\n", floatCount, floatPct);
  Serial.println("  --------------------------------------------------");
  Serial.println("                  |    ADC    |  Tegangan");
  Serial.println("  ----------------|-----------|--------------");
  Serial.printf ("  Minimum         |  %7d  |  %.4f V\n", minAdc, minV);
  Serial.printf ("  Maksimum        |  %7d  |  %.4f V\n", maxAdc, maxV);
  Serial.printf ("  Rata-rata       |  %9.1f|  %.4f V\n", meanAdc, meanV);
  Serial.printf ("  Peak-to-peak    |  %7d  |  %.4f V\n", p2pAdc, p2pV);
  Serial.printf ("  Std deviasi     |  %9.1f|  %.4f V\n", stdAdc, stdV);
  Serial.println("  --------------------------------------------------");

  if (isPH) {
    Serial.printf ("  pH (dari mean)  : %.2f\n", computePH(meanV));
    Serial.println("  --------------------------------------------------");
    Serial.println("  Skala kalibrasi saat ini (referensi tegangan):");
    Serial.printf ("    pH 4.01  ->  V = %.4f\n", calV4);
    Serial.printf ("    pH 6.86  ->  V = %.4f\n", calV7);
    Serial.printf ("    pH 8.00  ->  V = %.4f\n", voltageForPH(8.0f));
  } else {
    Serial.printf ("  NTU (dari mean) : %.1f\n", computeNTU(meanV));
  }
  Serial.println("  --------------------------------------------------");

  // ── Interpretasi otomatis ──────────────────────────────────────────
  Serial.println("  >> INTERPRETASI:");
  if (minAdc >= 4090) {
    Serial.println("  !! SATURASI ATAS — sinyal MENTOK di 3.3V (ADC 4095).");
    Serial.println("  Tegangan asli kemungkinan >3.3V, di luar jangkauan");
    Serial.println("  ADC. Ini BUKAN pembacaan valid — modul sensor");
    Serial.println("  terdorong ke rail. Penyebab khas: ground loop /");
    Serial.println("  beda potensial air vs ground rangkaian.");
    Serial.println("  Solusi: ISOLASI listrik, bukan kalibrasi ulang.");
  } else if (maxAdc <= 10) {
    Serial.println("  !! SATURASI BAWAH — sinyal MENTOK di 0V (ADC 0).");
    Serial.println("  Ini BUKAN pembacaan valid — modul sensor terdorong");
    Serial.println("  ke rail. Cek koneksi & kemungkinan ground loop.");
  } else if (maxAdc >= 4095) {
    Serial.println("  !! Sebagian sinyal MENTOK di rail (clipping).");
    Serial.println("  Puncak sinyal melewati 3.3V — pembacaan tidak");
    Serial.println("  akurat penuh. Curigai interferensi / ground loop.");
  } else if (floatPct > 30.0f) {
    Serial.println("  Banyak sampel FLOATING. Koneksi probe longgar");
    Serial.println("  atau probe tidak tercelup. Perbaiki koneksi dulu.");
  } else if (p2pV > 0.25f || stdV > 0.06f) {
    Serial.printf ("  RIAK BESAR (p2p %.3fV, std %.3fV).\n", p2pV, stdV);
    Serial.println("  Kuat indikasi TEGANGAN NYASAR (stray voltage) AC");
    Serial.println("  dari kincir/pompa. Ini masalah kelistrikan, bukan");
    Serial.println("  program — filter apa pun tetap baca angka salah.");
  } else if (p2pV > 0.08f || stdV > 0.02f) {
    Serial.printf ("  RIAK SEDANG (p2p %.3fV, std %.3fV).\n", p2pV, stdV);
    Serial.println("  Ada interferensi ringan. Bandingkan dengan hasil");
    Serial.println("  DIAG di gelas untuk memastikan.");
  } else {
    Serial.printf ("  STABIL (p2p %.3fV, std %.3fV).\n", p2pV, stdV);
    Serial.println("  Tidak ada riak AC. Kalau pH tetap beda jauh dari");
    Serial.println("  gelas, ini OFFSET DC: bandingkan 'Rata-rata V'");
    Serial.println("  hasil di gelas vs di tambak.");
  }
  Serial.println("  --------------------------------------------------");
  Serial.println("  CARA PAKAI:");
  Serial.println("  1. DIAG saat probe di GELAS  -> catat Rata-rata V");
  Serial.println("  2. DIAG saat probe di TAMBAK -> catat Rata-rata V");
  Serial.println("  3. Rata-rata V geser jauh / p2p & std jauh lebih");
  Serial.println("     besar di tambak  =>  stray voltage terkonfirmasi.");
  Serial.println("  4. Ulangi di tambak dengan kincir/pompa DIMATIKAN —");
  Serial.println("     kalau angka balik normal, sumbernya kincir/pompa.");
  Serial.println("═══════════════════════════════════════════════════\n");

  char line1[17], line2[17];
  if (isPH) snprintf(line1, sizeof(line1), "pH:%.2f %.3fV", computePH(meanV), meanV);
  else      snprintf(line1, sizeof(line1), "NTU:%.0f %.2fV", computeNTU(meanV), meanV);
  snprintf(line2, sizeof(line2), "p2p%.2f sd%.2f", p2pV, stdV);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);

  delay(5000);
  lastReadMillis = millis();
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

  } else if (cmd.startsWith("CAL1 ")) {
    // ── Kalibrasi 1-Titik (single-point) ─────────────────────────────────
    // Slope kalibrasi lama dipertahankan; hanya offset yang digeser supaya
    // titik (voltase terukur, pH referensi) tepat jatuh di garis kalibrasi.
    // Berguna saat tidak punya buffer 4.01 & 6.86 — cukup 1 air yang pH-nya
    // sudah diketahui dari alat ukur. Contoh: CAL1 7.4
    float refPH = cmd.substring(5).toFloat();
    if (refPH < 0.5f || refPH > 13.5f) {
      Serial.println("\n  ✗ pH referensi tidak valid. Isi 0.5 - 13.5.");
      Serial.println("    Contoh: CAL1 7.4");
      lcdPrint("CAL1 GAGAL", "pH ref invalid"); return;
    }
    if (abs(calV7 - calV4) < 0.001f) {
      Serial.println("\n  ✗ Slope kalibrasi lama tidak valid.");
      Serial.println("    Lakukan kalibrasi 2-titik (CAL4/CAL7) lebih dulu.");
      lcdPrint("CAL1 GAGAL", "Slope invalid"); return;
    }

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║   KALIBRASI 1-TITIK — Sampling...    ║");
    Serial.println("╚══════════════════════════════════════╝");
    char l1[17]; snprintf(l1, sizeof(l1), "CAL1 pH %.2f", refPH);
    lcdPrint(l1, "Sampling...");

    float v = readVoltageCalibration(PH_PIN, 10000);
    if (v < 0) {
      Serial.println("  ✗ ERROR: Sinyal floating. Cek koneksi probe!");
      lcdPrint("CAL1 GAGAL", "Cek koneksi!"); return;
    }

    // Slope lama (pH/V) & jarak voltase antar-anchor — keduanya dipertahankan.
    float m  = (PH_REF_7 - PH_REF_4) / (calV7 - calV4);
    float dV = calV7 - calV4;

    // Garis baru: pH = m*x + b  →  b = refPH - m*v
    // computePH() menurunkan b dari calV7:  b = PH_REF_7 - m*calV7
    //   → calV7 = (PH_REF_7 - b) / m  ;  calV4 = calV7 - dV
    float b     = refPH - m * v;
    float newV7 = (PH_REF_7 - b) / m;
    float newV4 = newV7 - dV;

    preferences.begin("ph_cal", false);
    preferences.putFloat("v4", newV4);
    preferences.putFloat("v7", newV7);
    preferences.end();
    calV4 = newV4; calV7 = newV7;
    cal4Set = false; cal7Set = false;

    Serial.println("\n  ✓ KALIBRASI 1-TITIK BERHASIL DISIMPAN");
    Serial.printf("  ✓ V terukur  = %.4f V (ADC ~%d)\n", v, (int)(v / 3.3f * 4095.0f));
    Serial.printf("  ✓ pH acuan   = %.2f\n", refPH);
    Serial.printf("  ✓ Slope      = %.4f pH/V (tetap dari kalibrasi lama)\n", m);
    Serial.printf("  ✓ calV4 baru = %.4f V\n", newV4);
    Serial.printf("  ✓ calV7 baru = %.4f V\n", newV7);
    Serial.println("  → Tersimpan ke NVS. Cek dengan READ, lalu upload FW utama.\n");

    char l2[17]; snprintf(l2, sizeof(l2), "pH%.2f V=%.2f", refPH, v);
    lcdPrint("CAL1 SAVED!", l2);

  } else if (cmd == "TCAL0" || cmd == "TCAL70" || cmd == "TCAL400" || cmd == "TCALSAVE") {
    Serial.println("\n  ⚠ Kalibrasi turbidity di-hardcode di kalibrasi.ino.");
    Serial.println("    Ubah TURB_V_CLEAR / TURB_V_TURBID lalu re-flash untuk mengganti.");
    lcdPrint("TURB hardcoded", "Edit + re-flash");

  } else if (cmd == "CALINFO") {
    Serial.println("\n═══════════════════════════════════════");
    Serial.println("  STATUS KALIBRASI SENSOR");
    Serial.println("═══════════════════════════════════════");
    Serial.println("  -- pH (2-titik) --");
    Serial.printf("  V(pH4.01) = %.4f V\n", calV4);
    Serial.printf("  V(pH6.86) = %.4f V\n", calV7);
    Serial.printf("  Sesi: CAL4=%s CAL7=%s\n",
                  cal4Set?"✓":"—", cal7Set?"✓":"—");
    Serial.println("  -- Turbidity (2-titik HARDCODED) --");
    Serial.printf("  V(0   NTU) = %.4f V\n", TURB_V_CLEAR);
    Serial.printf("  V(400 NTU) = %.4f V\n", TURB_V_TURBID);

    char buf[17];
    lcd.clear();
    snprintf(buf, sizeof(buf), "p4:%.2f p7:%.2f", calV4, calV7);
    lcd.setCursor(0, 0); lcd.print(buf);
    snprintf(buf, sizeof(buf), "Tb HC %.2f %.2f", TURB_V_CLEAR, TURB_V_TURBID);
    lcd.setCursor(0, 1); lcd.print(buf);

  } else if (cmd == "READ") {
    continuousRead = !continuousRead;
    Serial.printf("[Read] %s\n", continuousRead ? "AKTIF" : "NONAKTIF");
    if (!continuousRead) lcdPrint("pH Cal Tool", "Ketik perintah..");

  } else if (cmd == "DIAG" || cmd == "DIAGPH") {
    runDiagnostic(PH_PIN, "pH", true);

  } else if (cmd == "DIAGTURB") {
    runDiagnostic(TURB_PIN, "Turbidity", false);

  } else if (cmd.startsWith("ERRPH ")) {
    float ref = cmd.substring(6).toFloat();
    float vals[ERR_SAMPLES];
    for (int i = 0; i < ERR_SAMPLES; i++) vals[i] = -999.0f;
    int validCount = 0;

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║    UJI ERROR pH — 5x Pengukuran     ║");
    Serial.println("╚══════════════════════════════════════╝");

    for (int i = 0; i < ERR_SAMPLES; i++) {
      char lcdLine[17];
      snprintf(lcdLine, sizeof(lcdLine), "UJI pH [%d/%d]", i + 1, ERR_SAMPLES);
      lcdPrint(lcdLine, "Mengukur...");

      // 15x readVoltage (masing-masing trimmed mean 50 sampel) lalu rata-ratakan
      const int N_AVG = 15;
      float vSum = 0;
      int   vGood = 0;
      for (int s = 0; s < N_AVG; s++) {
        float v = readVoltage(PH_PIN);
        if (v >= 0) { vSum += v; vGood++; }
        delay(200);
      }
      if (vGood > 5) { vals[i] = computePH(vSum / vGood); validCount++; }
    }

    if (validCount < 3) {
      Serial.println("  ✗ Terlalu banyak NO SIGNAL. Cek koneksi probe!");
      lcdPrint("ERR: NO SIGNAL", "Cek koneksi!"); return;
    }

    float sum = 0;
    for (int i = 0; i < ERR_SAMPLES; i++) if (vals[i] > -100.0f) sum += vals[i];
    float avg    = sum / validCount;
    float absErr = abs(avg - ref);
    float relErr = (abs(ref) > 0.001f) ? (absErr / abs(ref)) * 100.0f : 0.0f;

    Serial.println("\n═══════════════════════════════════════════════════════");
    Serial.println("  TABEL UJI AKURASI — pH");
    Serial.println("═══════════════════════════════════════════════════════");
    Serial.println("  No. | Ref (pH) | Sensor (pH) | Err Abs | Err Rel (%)");
    Serial.println("  ----|----------|-------------|---------|------------");
    for (int i = 0; i < ERR_SAMPLES; i++) {
      if (vals[i] < -100.0f) {
        Serial.printf("  %3d | %8.2f |   NO SIGNAL |       - |           -\n", i+1, ref);
      } else {
        float e  = abs(vals[i] - ref);
        float re = (abs(ref) > 0.001f) ? (e / abs(ref)) * 100.0f : 0.0f;
        Serial.printf("  %3d | %8.2f |    %8.2f | %7.4f |      %6.2f%%\n", i+1, ref, vals[i], e, re);
      }
    }
    Serial.println("  ----|----------|-------------|---------|------------");
    Serial.printf("  Avg | %8.2f |    %8.2f | %7.4f |      %6.2f%%\n", ref, avg, absErr, relErr);
    Serial.println("═══════════════════════════════════════════════════════");
    Serial.println("  >> Silakan copy tabel. Lanjut otomatis dalam 5 detik...");
    Serial.println("═══════════════════════════════════════════════════════\n");
    delay(5000);
    lastReadMillis = millis();

    char line1[17], line2[17];
    snprintf(line1, sizeof(line1), "pH:%.2f Ref:%.2f", avg, ref);
    snprintf(line2, sizeof(line2), "Err:%.2f(%.1f%%)", absErr, relErr);
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(line1);
    lcd.setCursor(0, 1); lcd.print(line2);

  } else if (cmd.startsWith("ERRTURB ")) {
    float ref = cmd.substring(8).toFloat();
    float vals[ERR_SAMPLES];
    for (int i = 0; i < ERR_SAMPLES; i++) vals[i] = -999.0f;
    int validCount = 0;

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║  UJI ERROR TURBIDITY — 5x Ukur      ║");
    Serial.println("╚══════════════════════════════════════╝");

    for (int i = 0; i < ERR_SAMPLES; i++) {
      char lcdLine[17];
      snprintf(lcdLine, sizeof(lcdLine), "UJI TURB [%d/%d]", i + 1, ERR_SAMPLES);
      lcdPrint(lcdLine, "Mengukur...");

      const int N_AVG = 15;
      float vSum = 0;
      int   vGood = 0;
      for (int s = 0; s < N_AVG; s++) {
        float v = readVoltage(TURB_PIN);
        if (v >= 0) { vSum += v; vGood++; }
        delay(200);
      }
      if (vGood > 5) { vals[i] = computeNTU(vSum / vGood); validCount++; }
    }

    if (validCount < 3) {
      Serial.println("  ✗ Terlalu banyak NO SIGNAL. Cek koneksi sensor!");
      lcdPrint("ERR: NO SIGNAL", "Cek koneksi!"); return;
    }

    float sum = 0;
    for (int i = 0; i < ERR_SAMPLES; i++) if (vals[i] > -100.0f) sum += vals[i];
    float avg    = sum / validCount;
    float absErr = abs(avg - ref);
    bool  hasRef = abs(ref) > 0.001f;

    Serial.println("\n═══════════════════════════════════════════════════════════");
    Serial.println("  TABEL UJI AKURASI — TURBIDITY");
    if (!hasRef) Serial.println("  (Referensi = 0 NTU → Air Bersih, Error Rel tidak berlaku)");
    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("  No. | Ref (NTU) | Sensor (NTU) | Err Abs | Err Rel (%)");
    Serial.println("  ----|-----------|--------------|---------|------------");
    for (int i = 0; i < ERR_SAMPLES; i++) {
      if (vals[i] < -100.0f) {
        Serial.printf("  %3d | %9.1f |    NO SIGNAL |       - |           -\n", i+1, ref);
      } else {
        float e  = abs(vals[i] - ref);
        float re = hasRef ? (e / abs(ref)) * 100.0f : 0.0f;
        if (hasRef)
          Serial.printf("  %3d | %9.1f |    %9.1f | %7.2f |      %6.2f%%\n", i+1, ref, vals[i], e, re);
        else
          Serial.printf("  %3d | %9.1f |    %9.1f | %7.2f |         N/A\n", i+1, ref, vals[i], e);
      }
    }
    Serial.println("  ----|-----------|--------------|---------|------------");
    if (hasRef)
      Serial.printf("  Avg | %9.1f |    %9.1f | %7.2f |      %6.2f%%\n", ref, avg, absErr, (absErr/abs(ref))*100.0f);
    else
      Serial.printf("  Avg | %9.1f |    %9.1f | %7.2f |         N/A\n", ref, avg, absErr);
    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("  >> Silakan copy tabel. Lanjut otomatis dalam 5 detik...");
    Serial.println("═══════════════════════════════════════════════════════════\n");
    delay(5000);
    lastReadMillis = millis();

    char line1[17], line2[17];
    snprintf(line1, sizeof(line1), "T:%.1f Ref:%.0f", avg, ref);
    if (hasRef)
      snprintf(line2, sizeof(line2), "Err:%.1f(%.1f%%)", absErr, (absErr/abs(ref))*100.0f);
    else
      snprintf(line2, sizeof(line2), "Offset:%.2fNTU", avg);
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(line1);
    lcd.setCursor(0, 1); lcd.print(line2);

  } else if (cmd.startsWith("ERRTEMP ")) {
    float ref = cmd.substring(8).toFloat();
    float vals[ERR_SAMPLES];
    for (int i = 0; i < ERR_SAMPLES; i++) vals[i] = -999.0f;
    int validCount = 0;

    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║   UJI ERROR SUHU — 5x Pengukuran    ║");
    Serial.println("╚══════════════════════════════════════╝");

    for (int i = 0; i < ERR_SAMPLES; i++) {
      char lcdLine[17];
      snprintf(lcdLine, sizeof(lcdLine), "UJI SUHU [%d/%d]", i + 1, ERR_SAMPLES);
      lcdPrint(lcdLine, "Membaca...");
      tempSensors.requestTemperatures();
      float v = tempSensors.getTempCByIndex(0);
      delay(200);
      if (v > -126.0f) { vals[i] = v; validCount++; }
    }

    if (validCount < 3) {
      Serial.println("  ✗ DS18B20 tidak terdeteksi. Cek koneksi!");
      lcdPrint("ERR: NO DS18B20", "Cek koneksi!"); return;
    }

    float sum = 0;
    for (int i = 0; i < ERR_SAMPLES; i++) if (vals[i] > -100.0f) sum += vals[i];
    float avg    = sum / validCount;
    float absErr = abs(avg - ref);
    float relErr = (abs(ref) > 0.001f) ? (absErr / abs(ref)) * 100.0f : 0.0f;

    Serial.println("\n═══════════════════════════════════════════════════════");
    Serial.println("  TABEL UJI AKURASI — SUHU");
    Serial.println("═══════════════════════════════════════════════════════");
    Serial.println("  No. |  Ref (°C) | Sensor (°C) | Err Abs | Err Rel (%)");
    Serial.println("  ----|-----------|-------------|---------|------------");
    for (int i = 0; i < ERR_SAMPLES; i++) {
      if (vals[i] < -100.0f) {
        Serial.printf("  %3d | %9.1f |   NO SENSOR |       - |           -\n", i+1, ref);
      } else {
        float e  = abs(vals[i] - ref);
        float re = (abs(ref) > 0.001f) ? (e / abs(ref)) * 100.0f : 0.0f;
        Serial.printf("  %3d | %9.1f |   %9.2f | %7.2f |      %6.2f%%\n", i+1, ref, vals[i], e, re);
      }
    }
    Serial.println("  ----|-----------|-------------|---------|------------");
    Serial.printf("  Avg | %9.1f |   %9.2f | %7.2f |      %6.2f%%\n", ref, avg, absErr, relErr);
    Serial.println("═══════════════════════════════════════════════════════");
    Serial.println("  >> Silakan copy tabel. Lanjut otomatis dalam 5 detik...");
    Serial.println("═══════════════════════════════════════════════════════\n");
    delay(5000);
    lastReadMillis = millis();

    char line1[17], line2[17];
    snprintf(line1, sizeof(line1), "%.2fC Ref:%.1fC", avg, ref);
    snprintf(line2, sizeof(line2), "Err:%.2fC(%.1f%%)", absErr, relErr);
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(line1);
    lcd.setCursor(0, 1); lcd.print(line2);

  } else if (cmd.length() > 0) {
    Serial.println("  Perintah tidak dikenal.");
    Serial.println("  pH:   CAL4 | CAL7 | CALSAVE | CAL1 <pH>");
    Serial.println("  Turb: TCAL0 | TCAL70 | TCAL400 | TCALSAVE  (piecewise 3-titik)");
    Serial.println("  Info: CALINFO | READ");
    Serial.println("  Uji Error: ERRPH <ref> | ERRTURB <ref> | ERRTEMP <ref>");
    Serial.println("  Diagnosa stray voltage: DIAG (pH) | DIAGTURB");
    lcdPrint("Cmd tdk dikenal", "ERR?/CAL?/INFO?");
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
  tempSensors.begin();

  preferences.begin("ph_cal", true);
  calV4 = preferences.getFloat("v4", 3.1228f);
  calV7 = preferences.getFloat("v7", 2.0752f);
  preferences.end();

  // Turbidity kalibrasi di-hardcode (TURB_V_CLEAR / TURB_V_TURBID), tidak baca NVS.

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║   SMART BUOY — CALIBRATION TOOL      ║");
  Serial.println("╠══════════════════════════════════════╣");
  Serial.printf ("║  V(pH4.01) = %.4f V (NVS)           ║\n", calV4);
  Serial.printf ("║  V(pH6.86) = %.4f V (NVS)           ║\n", calV7);
  Serial.printf ("║  V(0   NTU)= %.4f V (HARDCODED)     ║\n", TURB_V_CLEAR);
  Serial.printf ("║  V(400 NTU)= %.4f V (HARDCODED)     ║\n", TURB_V_TURBID);
  Serial.println("╠══════════════════════════════════════╣");
  Serial.println("║  CAL4  | CAL7   | CALSAVE            ║");
  Serial.println("║  (TCAL* dinonaktifkan - hardcoded)   ║");
  Serial.println("║  READ  | CALINFO                     ║");
  Serial.println("╠══════════════════════════════════════╣");
  Serial.println("║  Diagnosa stray voltage / ground loop║");
  Serial.println("║  DIAG (pH)  |  DIAGTURB              ║");
  Serial.println("╠══════════════════════════════════════╣");
  Serial.println("║  Kalibrasi 1-titik (tanpa buffer):   ║");
  Serial.println("║  CAL1 <pH>  contoh: CAL1 7.40        ║");
  Serial.println("╠══════════════════════════════════════╣");
  Serial.println("║  Uji Error (masukkan nilai referensi)║");
  Serial.println("║  ERRPH <pH>  contoh: ERRPH 6.86      ║");
  Serial.println("║  ERRTURB <NTU> contoh: ERRTURB 0     ║");
  Serial.println("║  ERRTEMP <°C>  contoh: ERRTEMP 27.5  ║");
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
