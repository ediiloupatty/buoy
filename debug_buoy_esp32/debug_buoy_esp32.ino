/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Edi Loupatty
 *
 * This file is part of the Smart Buoy IoT System.
 */

/**
 * @file debug_buoy_esp32.ino
 * @brief SIMULASI BUOY di ESP32 — jalankan OTAK kontrol buoy tanpa sensor fisik.
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │  UPLOAD KE: ESP32 + modul LoRa SX1278 (pengganti sementara buoy).│
 * └─────────────────────────────────────────────────────────────────┘
 *
 * Bedanya dengan debug_buoy_tx (ESP8266): sketch ini TIDAK sekadar timer.
 * Ia meniru buoy asli (buoy_transmitter) selengkapnya:
 *   1. Nilai pH & suhu DISIMULASIKAN sebagai gelombang yang naik-turun.
 *   2. Dijalankan lewat LOGIKA KONTROL yang sama (evaluateControl):
 *        • suhu ≥ TEMP_PUMP_ON_C  → pompa cooling ON  (histeresis)
 *        • suhu ≤ TEMP_PUMP_OFF_C → pompa cooling OFF
 *        • pH ≤ PH_DOSE_BELOW     → naikkan doseId (1 dosis kapur), dgn
 *          cooldown + batas harian, doseId di-persist ke NVS.
 *   3. Perintah dikirim ke node ESP8266 lewat LoRa, FORMAT SAMA PERSIS:
 *        CMD|P|<state>|0|<seq>          → node_pompa
 *        CMD|K|<doseId>|<openMs>|<seq>  → node_kapur
 *
 * Karena protokol identik dengan buoy asli, saat buoy sungguhan dipasang di
 * tambak, node_pompa & node_kapur langsung tersambung tanpa ubah apa pun.
 *
 * Kontrol via Serial Monitor (115200, akhiri Enter):
 *   s → tampilkan status (sensor sim + keputusan)
 *   h → paksa suhu PANAS sekejap (uji pompa ON tanpa menunggu gelombang)
 *   c → paksa suhu DINGIN sekejap (uji pompa OFF)
 *   k → DEBUG: paksa 1 dosis kapur SEKARANG (servo gerak, abaikan cooldown)
 *   p → paksa pH RENDAH sekejap (uji dosis kapur, pakai cooldown spt buoy asli)
 *   f → bekukan / lanjutkan gelombang simulasi (freeze)
 *
 * Board: "ESP32 Dev Module".  Library: LoRa (Sandeep Mistry).
 */

#include <math.h>
#include <SPI.h>
#include <LoRa.h>
#include <Preferences.h>   // NVS — persist doseId agar tahan reboot (seperti buoy)

#include "Config.h"

// ── Telemetry tersimulasi ────────────────────────────────────────────────────
float phValue = SIM_PH_BASE;
float tempC   = SIM_TEMP_BASE;

// ── Status modul LoRa ────────────────────────────────────────────────────────
bool loraReady = false;

// ── KONTROL AKTUATOR (identik dengan buoy asli) ──────────────────────────────
bool          coolPumpOn   = false;  ///< desired state pompa cooling (suhu)
uint32_t      doseId       = 0;      ///< id dosis kapur terkini (0 = belum pernah)
Preferences   dosePrefs;             ///< NVS handle — namespace "dose"
unsigned long lastDoseMs   = 0;      ///< millis() dosis terakhir (cooldown)
bool          everDosed    = false;  ///< sudah pernah dosis?
int           dosesToday   = 0;      ///< dosis dalam jendela 24 jam berjalan
unsigned long dayWindowMs  = 0;      ///< awal jendela 24 jam (millis)

// ── Simulasi ─────────────────────────────────────────────────────────────────
uint32_t      seq          = 0;      ///< penghitung kirim, naik per paket (spt buoy)
bool          simFrozen    = false;  ///< true = gelombang berhenti (nilai ditahan)
long          tempForceMs  = 0;      ///< sisa waktu override suhu (h/c)
long          phForceMs    = 0;      ///< sisa waktu override pH (p)
float         tempForceVal = 0;
float         phForceVal   = 0;

// ── Forward declarations ─────────────────────────────────────────────────────
bool initLoRa();
void simulateSensors();
void evaluateControl();
void sendPumpCommand();
void sendKapurCommand();
void sendActuatorCommands();
void handleSerial();
void printStatus();

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n══════════════════════════════════════════");
  Serial.println("  DEBUG BUOY ESP32 — simulasi OTAK buoy");
  Serial.println("══════════════════════════════════════════");

  loraReady = initLoRa();
  if (!loraReady) {
    Serial.println("[LoRa] ✗ Init GAGAL — cek wiring modul. Perintah TX di-skip.");
  }

  // Muat doseId dari NVS — persis buoy asli, supaya reboot tidak me-reset
  // acuan dosis di node_kapur.
  dosePrefs.begin("dose", true);  // read-only
  doseId = dosePrefs.getUInt("id", 0);
  dosePrefs.end();
  if (doseId > 0) {
    everDosed  = true;
    lastDoseMs = millis();
    Serial.printf("[Kontrol] doseId=%lu dipulihkan dari NVS (cooldown mulai ulang)\n",
                  (unsigned long)doseId);
  }

  Serial.println("\n[INFO] Perintah Serial: s=status  h=panas  c=dingin  k=tabur kapur  p=pH rendah  f=freeze");
  Serial.printf("[INFO] Ambang: pompa ON≥%.1f°C / OFF≤%.1f°C | kapur pH≤%.1f | cooldown %lus\n\n",
                TEMP_PUMP_ON_C, TEMP_PUMP_OFF_C, PH_DOSE_BELOW,
                DOSE_COOLDOWN_MS / 1000UL);

  // Evaluasi awal + kirim state aman default (seperti buoy asli di setup).
  dayWindowMs = millis();
  simulateSensors();
  evaluateControl();
  sendActuatorCommands();
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
  handleSerial();

  // ── 1. Sampling sensor sim berkala → evaluasi → kirim perintah ──────────────
  static unsigned long lastSampleAt = 0;
  unsigned long now = millis();
  if (now - lastSampleAt >= SIM_SAMPLE_INTERVAL_MS) {
    lastSampleAt = now;
    simulateSensors();
    Serial.printf("[Sensor sim] Suhu=%.1f°C | pH=%.2f%s\n",
                  tempC, phValue, simFrozen ? "  (FREEZE)" : "");
    evaluateControl();
    sendActuatorCommands();
  }

  // ── 2. Re-broadcast perintah berkala (jaga link + fail-safe node) ───────────
  static unsigned long lastCmdAt = 0;
  if (now - lastCmdAt >= CMD_REBROADCAST_MS) {
    lastCmdAt = now;
    sendActuatorCommands();
  }

  delay(50);
}

// =============================================================================
// SIMULASI SENSOR — gelombang sinus + override manual sementara
// =============================================================================
void simulateSensors() {
  // Kurangi sisa waktu override (tiap sampel/loop panggil).
  static unsigned long lastTick = 0;
  unsigned long now = millis();
  unsigned long dt  = (lastTick == 0) ? 0 : (now - lastTick);
  lastTick = now;
  if (tempForceMs > 0) tempForceMs -= (long)dt;
  if (phForceMs   > 0) phForceMs   -= (long)dt;

  if (!simFrozen) {
    float t = (float)now;
    tempC   = SIM_TEMP_BASE + SIM_TEMP_AMPL *
              sinf(2.0f * (float)M_PI * t / (float)SIM_TEMP_PERIOD);
    phValue = SIM_PH_BASE + SIM_PH_AMPL *
              sinf(2.0f * (float)M_PI * t / (float)SIM_PH_PERIOD + SIM_PH_PHASE);
  }

  // Override manual menang selama masih aktif (uji terarah tanpa menunggu).
  if (tempForceMs > 0) tempC   = tempForceVal;
  if (phForceMs   > 0) phValue = phForceVal;
}

// =============================================================================
// CONTROL ENGINE — SALINAN PERSIS logika buoy_transmitter.ino
// =============================================================================
void evaluateControl() {
  unsigned long now = millis();

  // Reset jendela harian tiap 24 jam (rolling, tanpa NTP).
  if (now - dayWindowMs >= 24UL * 60UL * 60UL * 1000UL) {
    dayWindowMs = now;
    dosesToday  = 0;
  }

  // ── Pompa cooling: histeresis suhu ──
  if (tempC > -126.0f) {  // abaikan fault DS18B20 (di buoy asli)
    if (tempC >= TEMP_PUMP_ON_C)       coolPumpOn = true;
    else if (tempC <= TEMP_PUMP_OFF_C) coolPumpOn = false;
  }

  // ── Dispenser kapur: pH rendah + cooldown + batas harian ──
  if (phValue > 0.0f && phValue <= PH_DOSE_BELOW) {
    bool cooldownOK = (!everDosed) || (now - lastDoseMs >= DOSE_COOLDOWN_MS);
    if (cooldownOK && dosesToday < DOSE_MAX_PER_DAY) {
      doseId++;
      lastDoseMs = now;
      everDosed  = true;
      dosesToday++;

      dosePrefs.begin("dose", false);
      dosePrefs.putUInt("id", doseId);
      dosePrefs.end();
      Serial.printf("[Kontrol] pH %.2f ≤ %.2f → DOSIS kapur #%lu (hari ini: %d/%d)\n",
                    phValue, PH_DOSE_BELOW, (unsigned long)doseId,
                    dosesToday, DOSE_MAX_PER_DAY);
    } else {
      Serial.printf("[Kontrol] pH %.2f rendah tapi %s — tunda dosis.\n",
                    phValue, cooldownOK ? "batas harian tercapai" : "masih cooldown");
    }
  }

  Serial.printf("[Kontrol] Pompa cooling=%s | doseId=%lu\n",
                coolPumpOn ? "ON" : "OFF", (unsigned long)doseId);
}

/**
 * @brief Kirim paket perintah ke node aktuator via LoRa — FORMAT identik buoy.
 */
void sendPumpCommand() {
  if (!loraReady) return;
  String pkt = String(LORA_CMD_PREFIX) + "|" + String(CMD_DST_PUMP) + "|" +
               String(coolPumpOn ? 1 : 0) + "|0|" + String(++seq);
  LoRa.beginPacket(); LoRa.print(pkt); int ok = LoRa.endPacket();
  Serial.printf("[LoRa CMD] %s → %s\n", ok ? "✓" : "✗", pkt.c_str());
}

void sendKapurCommand() {
  if (!loraReady) return;
  String pkt = String(LORA_CMD_PREFIX) + "|" + String(CMD_DST_KAPUR) + "|" +
               String((unsigned long)doseId) + "|" + String(DOSE_SERVO_OPEN_MS) +
               "|" + String(++seq);
  LoRa.beginPacket(); LoRa.print(pkt); int ok = LoRa.endPacket();
  Serial.printf("[LoRa CMD] %s → %s\n", ok ? "✓" : "✗", pkt.c_str());
}

/** @brief Kirim kedua perintah (pompa lalu kapur) — dipakai siklus normal. */
void sendActuatorCommands() {
  sendPumpCommand();
  sendKapurCommand();
}

// =============================================================================
// LoRa
// =============================================================================
bool initLoRa() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) return false;

  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.enableCrc();

  Serial.printf("[LoRa] ✓ TX siap @ %.0f MHz | SF%d\n",
                (double)LORA_FREQUENCY / 1e6, LORA_SPREADING_FACTOR);
  return true;
}

// =============================================================================
// SERIAL — kontrol demo
// =============================================================================
void handleSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;

  char c = cmd.charAt(0);
  switch (c) {
    case 's': case 'S':
      printStatus();
      break;
    case 'h': case 'H':                       // paksa panas → pompa ON
      tempForceVal = TEMP_PUMP_ON_C + 1.0f;
      tempForceMs  = 10000;                   // tahan 10 detik
      Serial.printf("[Sim] Paksa suhu PANAS %.1f°C selama 10 dtk.\n", tempForceVal);
      break;
    case 'c': case 'C':                       // paksa dingin → pompa OFF
      tempForceVal = TEMP_PUMP_OFF_C - 1.0f;
      tempForceMs  = 10000;
      Serial.printf("[Sim] Paksa suhu DINGIN %.1f°C selama 10 dtk.\n", tempForceVal);
      break;
    case 'k': case 'K': {                     // DEBUG: paksa 1 dosis kapur SEKARANG
      // Naikkan doseId langsung, abaikan cooldown & batas harian — murni untuk
      // menguji "apakah ESP32 ↔ node kapur nyambung": servo harus buka-tutup.
      doseId++;
      lastDoseMs = millis();                  // reset cooldown agar 'p' setelahnya wajar
      everDosed  = true;
      dosePrefs.begin("dose", false);
      dosePrefs.putUInt("id", doseId);
      dosePrefs.end();
      Serial.printf("[Debug] Paksa DOSIS kapur #%lu (abaikan cooldown) → servo harus gerak.\n",
                    (unsigned long)doseId);
      sendKapurCommand();   // kirim paket kapur DULUAN & sendiri → servo respons tercepat
      break;
    }
    case 'p': case 'P':                       // paksa pH rendah → dosis kapur
      phForceVal = PH_DOSE_BELOW - 0.3f;
      phForceMs  = 10000;
      Serial.printf("[Sim] Paksa pH RENDAH %.2f selama 10 dtk.\n", phForceVal);
      break;
    case 'f': case 'F':                       // freeze / lanjut gelombang
      simFrozen = !simFrozen;
      Serial.printf("[Sim] Gelombang %s.\n", simFrozen ? "DIBEKUKAN" : "dilanjutkan");
      break;
    default:
      Serial.println("[Sim] ⚠ Perintah tak dikenal (pakai s/h/c/k/p/f).");
      break;
  }
}

void printStatus() {
  Serial.println("── STATUS ─────────────────────────────────");
  Serial.printf("  Suhu sim   : %.1f°C  (ON≥%.1f / OFF≤%.1f)\n",
                tempC, TEMP_PUMP_ON_C, TEMP_PUMP_OFF_C);
  Serial.printf("  pH sim     : %.2f    (dosis bila ≤%.1f)\n", phValue, PH_DOSE_BELOW);
  Serial.printf("  Pompa cool : %s\n", coolPumpOn ? "ON" : "OFF");
  Serial.printf("  doseId     : %lu  (hari ini %d/%d)\n",
                (unsigned long)doseId, dosesToday, DOSE_MAX_PER_DAY);
  Serial.printf("  Gelombang  : %s\n", simFrozen ? "FREEZE" : "jalan");
  Serial.println("───────────────────────────────────────────");
}
