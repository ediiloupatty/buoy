/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Edi Loupatty
 *
 * This file is part of the Smart Buoy IoT System.
 */

/**
 * @file node_kapur.ino
 * @brief NODE KAPUR / pH DISPENSER — ESP8266 + LoRa RX + servo.
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │  UPLOAD KE: ESP8266 (NodeMCU / Wemos D1 mini) + modul LoRa SX1278│
 * └─────────────────────────────────────────────────────────────────┘
 *
 * Tugas:
 *   - Dengarkan perintah LoRa dari buoy: "CMD|K|<doseId>|<openMs>|<seq>"
 *   - Setiap doseId yang NAIK = tabur 1 dosis kapur: servo BUKA openMs → TUTUP.
 *   - doseId sama (paket re-broadcast) → TIDAK menabur ulang (dedup).
 *   - Saat boot, doseId pertama yang terdengar dipakai sebagai acuan TANPA
 *     menabur, supaya reboot node tidak memicu dosis liar.
 *
 * Board: "LOLIN(WEMOS) D1 mini" / "NodeMCU 1.0".
 * Library: LoRa (Sandeep Mistry) + Servo (bawaan core ESP8266).
 */

#include <SPI.h>
#include <LoRa.h>
#include <Servo.h>
#include "Config.h"

Servo dispenser;

bool      loraReady   = false;
bool      doseSynced  = false;   // sudah punya acuan doseId dari buoy?
uint32_t  lastDoseId  = 0;       // doseId terakhir yang sudah diproses

bool initLoRa();
void handleCommand(const String &msg);
void runDose(unsigned long openMs);

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  // Servo ke posisi TERTUTUP sebelum apa pun.
  dispenser.attach(SERVO_PIN);
  dispenser.write(SERVO_CLOSED_DEG);

  Serial.println("\n══════════════════════════════════════════");
  Serial.println("  Smart Buoy — NODE KAPUR / pH (ESP8266)");
  Serial.println("══════════════════════════════════════════");

  loraReady = initLoRa();
  if (!loraReady) {
    Serial.println("[LoRa] ✗ Init GAGAL — cek wiring. Servo tetap tertutup (aman).");
  }
}

// =============================================================================
// LOOP — terima perintah
// =============================================================================
void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize > 0) {
    String msg = LoRa.readString();
    msg.trim();
    handleCommand(msg);
  }
  delay(10);
}

// =============================================================================
// LoRa
// =============================================================================
bool initLoRa() {
  SPI.begin();                                  // ESP8266: SPI pin fix (D5/D6/D7)
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) return false;

  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.enableCrc();

  Serial.printf("[LoRa] ✓ RX siap @ %.0f MHz | SF%d\n",
                (double)LORA_FREQUENCY / 1e6, LORA_SPREADING_FACTOR);
  return true;
}

/**
 * @brief Parse "CMD|K|<doseId>|<openMs>|<seq>"; tabur bila doseId naik.
 */
void handleCommand(const String &msg) {
  const int N = 5;
  String parts[N];
  int idx = 0, start = 0;
  for (int i = 0; i <= (int)msg.length() && idx < N; i++) {
    if (i == (int)msg.length() || msg.charAt(i) == '|') {
      parts[idx++] = msg.substring(start, i);
      start = i + 1;
    }
  }
  if (idx != N || parts[0] != LORA_CMD_PREFIX) return;
  if (parts[1].length() == 0 || parts[1].charAt(0) != CMD_DST_KAPUR) return;  // bukan untukku

  uint32_t      doseId = strtoul(parts[2].c_str(), nullptr, 10);
  unsigned long openMs = strtoul(parts[3].c_str(), nullptr, 10);
  if (openMs > DOSE_OPEN_MAX_MS) openMs = DOSE_OPEN_MAX_MS;   // clamp keamanan

  // Boot pertama: ambil acuan tanpa menabur.
  if (!doseSynced) {
    doseSynced = true;
    lastDoseId = doseId;
    Serial.printf("[CMD] Sinkron acuan doseId=%lu (tanpa menabur)\n",
                  (unsigned long)doseId);
    return;
  }

  if (doseId > lastDoseId) {
    Serial.printf("[CMD] Dosis kapur #%lu (RSSI %d dBm) → buka %lu ms\n",
                  (unsigned long)doseId, LoRa.packetRssi(), openMs);
    runDose(openMs);
    lastDoseId = doseId;
  } else if (doseId < lastDoseId) {
    // doseId MUNDUR = buoy reset dengan counter baru (mis. NVS terhapus /
    // ganti board). Re-sync acuan tanpa menabur, agar dosis berikutnya
    // (doseId+1) tidak tertelan menunggu counter lama terlampaui.
    lastDoseId = doseId;
    Serial.printf("[CMD] doseId mundur → re-sync acuan ke %lu (tanpa menabur)\n",
                  (unsigned long)doseId);
  }
  // doseId == lastDoseId → re-broadcast, abaikan (sudah ditabur).
}

/**
 * @brief Gerakkan servo: BUKA (kecepatan penuh) → tahan openMs → TUTUP.
 *        write() langsung ke sudut target = servo bergerak secepat
 *        kemampuan fisik motornya (SG90 ±150 ms untuk 90°).
 */
void runDose(unsigned long openMs) {
  dispenser.write(SERVO_OPEN_DEG);
  delay(openMs);
  dispenser.write(SERVO_CLOSED_DEG);
  Serial.println("[Servo] ✓ Selesai menabur — hopper tertutup kembali.");
}
