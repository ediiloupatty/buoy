/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Edi Loupatty
 *
 * This file is part of the Smart Buoy IoT System.
 */

/**
 * @file buoy_transmitter.ino
 * @brief NODE BUOY — baca sensor + kontrol pompa + KIRIM via LoRa.
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │  UPLOAD SKETCH INI KE: ESP32 yang ada DI BUOY (tengah tambak).   │
 * └─────────────────────────────────────────────────────────────────┘
 *
 * Tugas node ini:
 *   - Jalankan siklus pompa otomatis: FILLING → WAITING → DRAINING → loop
 *   - Saat WAITING (air tenang): sampling sensor pH / suhu / turbidity
 *   - Kirim hasilnya ke node RECEIVER lewat LoRa
 *   - TANPA WiFi, TANPA Firebase, TANPA NTP (semua itu di node RECEIVER)
 *
 * Format paket LoRa (plain text, pipe-delimited):
 *
 *     SB|<type>|<pump>|<phaseMs>|<dbg>|<pH>|<temp>|<turb>
 *
 *   type : 'L' = sampel sensor SEGAR saat air tenang
 *                → RECEIVER update /live + simpan untuk /history
 *          'P' = perubahan fase pompa (data sensor = nilai terakhir)
 *                → RECEIVER update /pump_status
 *
 * Contoh:  SB|L|WAITING|600000|0|7.82|29.5|42.0
 *
 * Kalibrasi pH: pakai sketch terpisah di folder kalibrasi/.
 */

#include <SPI.h>
#include <LoRa.h>

#include "Config.h"
#include "Sensors.h"

// ── Pump state machine ───────────────────────────────────────────────────────
int pumpState = PUMP_IDLE;  // di-set ke FILLING di setup()

// ── Telemetry terkini ────────────────────────────────────────────────────────
float phValue   = 0;
float tempC     = 0;
float turbidity = 0;

// ── Status modul LoRa ────────────────────────────────────────────────────────
// Bila init gagal (modul belum kebaca), JANGAN panggil fungsi TX LoRa — kalau
// dipaksa, endPacket() bisa nyangkut menunggu modul yang tidak ada → boot loop.
bool loraReady = false;

// ── Forward declarations ─────────────────────────────────────────────────────
void applyPumpRelay(int state);
unsigned long getPumpStateDuration(int state);
int nextPumpState(int state);
const char* currentPumpLabel();
const char* pumpStateLabel(int state);
bool initLoRa();
void sendLoRaPacket(char type);

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n══════════════════════════════════════════");
  Serial.println("  Smart Buoy IoT — NODE BUOY (LoRa TX)");
  Serial.println("══════════════════════════════════════════");

  // 1. Sensor
  initSensors();
  loadCalibration();

  // 2. Pompa — mulai langsung dari fase FILLING
  pinMode(PUMP_FILL_PIN,  OUTPUT);
  pinMode(PUMP_DRAIN_PIN, OUTPUT);
  pumpState = PUMP_FILLING;
  applyPumpRelay(pumpState);
  Serial.printf("[Pump] Siklus mulai — fase awal: %s\n", currentPumpLabel());

  // 3. LoRa
  loraReady = initLoRa();
  if (!loraReady) {
    Serial.println("[LoRa] ✗ Init GAGAL — cek wiring modul. Pompa tetap jalan (TX di-skip).");
  }

  // 4. Baca sensor pertama
  tempC     = readTemperature();
  phValue   = readPH(tempC);
  turbidity = readTurbidityNTU();
  Serial.printf("[Sensor] Suhu=%.1f°C | pH=%.2f | Turb=%.1f NTU\n",
                tempC, phValue, turbidity);

  // 5. Kirim status pompa awal ke receiver
  sendLoRaPacket('P');
}

// =============================================================================
// LOOP — siklus pompa otomatis + kirim LoRa
// =============================================================================
void loop() {
  static unsigned long stateStartedAt = millis();
  static unsigned long lastSampleAt   = 0;  // timer sampling saat WAITING

  unsigned long now     = millis();
  unsigned long elapsed = now - stateStartedAt;

  // ── 0. Perintah kalibrasi via Serial (CAL4/CAL7/CALSAVE/TURBV/CALINFO) ───────
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    handleCalibrationCommand(cmd);
  }

  // ── 1. Transisi fase pompa ──────────────────────────────────────────────────
  unsigned long duration = getPumpStateDuration(pumpState);
  if (elapsed >= duration) {
    int prevState = pumpState;
    pumpState = nextPumpState(pumpState);
    stateStartedAt = now;
    applyPumpRelay(pumpState);
    Serial.printf("[Pump] %s → %s (setelah %lu ms)\n",
                  pumpStateLabel(prevState), pumpStateLabel(pumpState), elapsed);

    if (pumpState == PUMP_WAITING) lastSampleAt = 0;  // paksa sampling pertama

    sendLoRaPacket('P');  // kabari receiver perubahan fase pompa
  }

  // ── 2. WAITING: endap → sampling berkala → kirim sampel SEGAR (type 'L') ─────
  if (pumpState == PUMP_WAITING && elapsed >= WAIT_SETTLE_MS) {
    if (lastSampleAt == 0 || (now - lastSampleAt >= WAIT_SAMPLE_INTERVAL_MS)) {
      lastSampleAt = now;
      tempC     = readTemperature();
      phValue   = readPH(tempC);
      turbidity = readTurbidityNTU();
      Serial.printf("[Sensor] WAITING sample → Suhu=%.1f°C | pH=%.2f | Turb=%.1f NTU\n",
                    tempC, phValue, turbidity);
      sendLoRaPacket('L');
    }
  }

  delay(50);  // cegah tight loop
}

// =============================================================================
// LoRa
// =============================================================================

/**
 * @brief Inisialisasi modul LoRa Ra-02 SX1278 dalam mode kirim.
 * @return true bila modul terdeteksi & siap.
 */
bool initLoRa() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    return false;
  }

  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.enableCrc();  // paket korup otomatis dibuang oleh radio

  Serial.printf("[LoRa] ✓ TX siap @ %.0f MHz | SF%d\n",
                (double)LORA_FREQUENCY / 1e6, LORA_SPREADING_FACTOR);
  return true;
}

/**
 * @brief Susun & kirim satu paket telemetry ke receiver via LoRa.
 * @param type 'L' = sampel sensor segar, 'P' = perubahan fase pompa.
 */
void sendLoRaPacket(char type) {
  if (!loraReady) {
    Serial.println("[LoRa TX] ⊘ Di-skip — modul LoRa belum siap.");
    return;
  }

  String pkt = String(LORA_PREFIX) + "|" +
               String(type) + "|" +
               String(currentPumpLabel()) + "|" +
               String(getPumpStateDuration(pumpState)) + "|" +
               String(PUMP_DEBUG_MODE) + "|" +
               String(phValue, 2) + "|" +
               String(tempC, 1) + "|" +
               String(turbidity, 1);

  LoRa.beginPacket();
  LoRa.print(pkt);
  int ok = LoRa.endPacket();  // blocking sampai terkirim; 1 = sukses

  Serial.printf("[LoRa TX] %s → %s\n", ok ? "✓" : "✗", pkt.c_str());
}

// =============================================================================
// PUMP CONTROL — state machine 3 fase (sama seperti versi single-ESP)
// =============================================================================

/**
 * @brief Apply state pompa ke pin relay. INTERLOCK: 2 pompa tak pernah ON bareng.
 */
void applyPumpRelay(int state) {
  switch (state) {
    case PUMP_FILLING:
      digitalWrite(PUMP_DRAIN_PIN, RELAY_OFF);
      digitalWrite(PUMP_FILL_PIN,  RELAY_ON);
      break;
    case PUMP_DRAINING:
      digitalWrite(PUMP_FILL_PIN,  RELAY_OFF);
      digitalWrite(PUMP_DRAIN_PIN, RELAY_ON);
      break;
    case PUMP_WAITING:
    case PUMP_IDLE:
    default:
      digitalWrite(PUMP_FILL_PIN,  RELAY_OFF);
      digitalWrite(PUMP_DRAIN_PIN, RELAY_OFF);
      break;
  }
}

unsigned long getPumpStateDuration(int state) {
  switch (state) {
    case PUMP_FILLING:  return PUMP_FILL_DURATION_MS;
    case PUMP_WAITING:  return PUMP_WAIT_DURATION_MS;
    case PUMP_DRAINING: return PUMP_DRAIN_DURATION_MS;
    default:            return 0;
  }
}

int nextPumpState(int state) {
  switch (state) {
    case PUMP_FILLING:  return PUMP_WAITING;
    case PUMP_WAITING:  return PUMP_DRAINING;
    case PUMP_DRAINING: return PUMP_FILLING;
    default:            return PUMP_FILLING;
  }
}

const char* currentPumpLabel() {
  return pumpStateLabel(pumpState);
}

const char* pumpStateLabel(int state) {
  switch (state) {
    case PUMP_FILLING:  return "FILLING";
    case PUMP_WAITING:  return "WAITING";
    case PUMP_DRAINING: return "DRAINING";
    case PUMP_IDLE:
    default:            return "IDLE";
  }
}
