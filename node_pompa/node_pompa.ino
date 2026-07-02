/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Edi Loupatty
 *
 * This file is part of the Smart Buoy IoT System.
 */

/**
 * @file node_pompa.ino
 * @brief NODE POMPA COOLING — ESP8266 + LoRa RX + relay.
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │  UPLOAD KE: ESP8266 (NodeMCU / Wemos D1 mini) + modul LoRa SX1278│
 * └─────────────────────────────────────────────────────────────────┘
 *
 * Tugas:
 *   - Dengarkan paket perintah LoRa dari buoy: "CMD|P|<state>|0|<seq>"
 *       state = 1 → relay ON  (pompa cooling jalan)
 *       state = 0 → relay OFF
 *   - State-based & idempotent: menerima "ON" berulang = tetap ON (tidak apa).
 *   - FAIL-SAFE: bila tak ada perintah valid > ACT_FAILSAFE_MS → pompa OFF.
 *
 * Board: "LOLIN(WEMOS) D1 mini" / "NodeMCU 1.0".  Library: LoRa (Sandeep Mistry).
 */

#include <SPI.h>
#include <LoRa.h>
#include "Config.h"

bool          loraReady   = false;
bool          relayState  = false;        // false = OFF
unsigned long lastCmdMs   = 0;            // millis() perintah valid terakhir (0 = belum)
unsigned long ledOffAtMs  = 0;            // kapan LED link harus padam (0 = sudah padam)

void setRelay(bool on);
bool initLoRa();
void handleCommand(const String &msg);

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  // Matikan relay PALING AWAL agar pompa tidak menyala saat boot.
  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false);

  // LED indikator link — padam sampai ada paket valid pertama.
  pinMode(LED_LINK_PIN, OUTPUT);
  digitalWrite(LED_LINK_PIN, LED_LINK_OFF);

  Serial.println("\n══════════════════════════════════════════");
  Serial.println("  Smart Buoy — NODE POMPA COOLING (ESP8266)");
  Serial.println("══════════════════════════════════════════");

  loraReady = initLoRa();
  if (!loraReady) {
    Serial.println("[LoRa] ✗ Init GAGAL — cek wiring. Relay tetap OFF (aman).");
  }
}

// =============================================================================
// LOOP — terima perintah + fail-safe
// =============================================================================
void loop() {
  // 1. Terima paket LoRa (polling)
  int packetSize = LoRa.parsePacket();
  if (packetSize > 0) {
    String msg = LoRa.readString();
    msg.trim();
    handleCommand(msg);
  }

  // 2. Fail-safe: link putus → matikan pompa
  if (lastCmdMs != 0 && (millis() - lastCmdMs > ACT_FAILSAFE_MS)) {
    if (relayState) {
      Serial.println("[Fail-safe] ⚠ Tidak ada perintah > batas — pompa di-OFF-kan.");
      setRelay(false);
    }
  }

  // 3. Padamkan LED link setelah kedipnya selesai
  if (ledOffAtMs != 0 && millis() >= ledOffAtMs) {
    ledOffAtMs = 0;
    digitalWrite(LED_LINK_PIN, LED_LINK_OFF);
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
 * @brief Parse "CMD|P|<state>|<v2>|<seq>" dan set relay sesuai state.
 *        Paket yang tujuannya bukan 'P' diabaikan.
 */
void handleCommand(const String &msg) {
  // Split jadi 5 field
  const int N = 5;
  String parts[N];
  int idx = 0, start = 0;
  for (int i = 0; i <= (int)msg.length() && idx < N; i++) {
    if (i == (int)msg.length() || msg.charAt(i) == '|') {
      parts[idx++] = msg.substring(start, i);
      start = i + 1;
    }
  }
  if (idx != N || parts[0] != LORA_CMD_PREFIX) return;          // bukan paket CMD
  if (parts[1].length() == 0 || parts[1].charAt(0) != CMD_DST_PUMP) return;  // bukan untukku

  lastCmdMs = millis();                       // perintah valid → reset timer fail-safe

  // Kedipkan LED link: bukti visual paket dari buoy/debug TX sampai.
  digitalWrite(LED_LINK_PIN, LED_LINK_ON);
  ledOffAtMs = millis() + LED_BLINK_MS;

  bool wantOn = (parts[2].toInt() != 0);

  if (wantOn != relayState) {
    Serial.printf("[CMD] Pompa cooling → %s (RSSI %d dBm)\n",
                  wantOn ? "ON" : "OFF", LoRa.packetRssi());
  }
  setRelay(wantOn);
}

// =============================================================================
// RELAY
// =============================================================================
void setRelay(bool on) {
  relayState = on;
  digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
}
