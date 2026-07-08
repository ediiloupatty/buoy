/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Edi Loupatty
 *
 * This file is part of the Smart Buoy IoT System.
 */

/**
 * @file test_pompa_siklus.ino
 * @brief PROGRAM KHUSUS uji siklus pompa buoy — TANPA sensor / LoRa / WiFi.
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │  UPLOAD KE: ESP32 buoy (board dengan relay pompa ISI & BUANG).   │
 * │  FQBN: esp32:esp32:esp32                                         │
 * └─────────────────────────────────────────────────────────────────┘
 *
 * Siklus berulang terus (loop):
 *   1. FILLING  — pompa ISI  ON  selama 40 detik
 *   2. WAITING  — kedua pompa OFF selama 60 detik (1 menit)
 *   3. DRAINING — pompa BUANG ON  selama 50 detik
 *   4. JEDA     — kedua pompa OFF selama 10 detik
 *   → balik ke FILLING (otomatis, berulang)
 *
 * INTERLOCK: pompa ISI & BUANG tidak pernah ON bersamaan (aman untuk relay).
 * Relay modul China umumnya active-LOW → ON = LOW, OFF = HIGH.
 */

// ── Pin relay pompa (sama dengan buoy_transmitter) ───────────────────────────
#define PUMP_FILL_PIN   25   ///< IN1 relay → pompa ISI (air masuk ke wadah)
#define PUMP_DRAIN_PIN  26   ///< IN2 relay → pompa BUANG (air wadah → tambak)
#define RELAY_ON        LOW  ///< modul relay active-LOW
#define RELAY_OFF       HIGH

// ── Durasi tiap fase (ms) — ubah di sini bila perlu ──────────────────────────
#define FILL_MS   40000UL    ///< FILLING  = 40 detik
#define WAIT_MS   60000UL    ///< WAITING  = 60 detik (1 menit)
#define DRAIN_MS  50000UL    ///< DRAINING = 50 detik
#define IDLE_MS   10000UL    ///< JEDA setelah draining = 10 detik

unsigned long cycleCount = 0;  // penghitung siklus untuk log

// ── Set kedua relay sekaligus (jaga interlock) ───────────────────────────────
void setPumps(bool fillOn, bool drainOn) {
  digitalWrite(PUMP_FILL_PIN,  fillOn  ? RELAY_ON : RELAY_OFF);
  digitalWrite(PUMP_DRAIN_PIN, drainOn ? RELAY_ON : RELAY_OFF);
}

// ── Jalankan satu fase: cetak status → tahan selama durasi (dengan hitung mundur) ──
void runPhase(const char* nama, bool fillOn, bool drainOn, unsigned long durMs) {
  setPumps(fillOn, drainOn);
  Serial.printf("\n[Fase] %-9s | ISI=%s BUANG=%s | durasi %lu dtk\n",
                nama, fillOn ? "ON " : "OFF", drainOn ? "ON " : "OFF", durMs / 1000UL);

  unsigned long start = millis();
  unsigned long lastTick = 0;
  while (millis() - start < durMs) {
    unsigned long sisa = (durMs - (millis() - start) + 999UL) / 1000UL;  // detik sisa (bulat atas)
    if (sisa != lastTick) {                                             // cetak tiap detik berubah
      lastTick = sisa;
      Serial.printf("   %s ... sisa %2lu dtk\r", nama, sisa);
    }
    delay(50);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // Pastikan kedua pompa OFF sebelum apa pun (aman saat boot).
  pinMode(PUMP_FILL_PIN,  OUTPUT);
  pinMode(PUMP_DRAIN_PIN, OUTPUT);
  setPumps(false, false);

  Serial.println("\n══════════════════════════════════════════════");
  Serial.println("  UJI SIKLUS POMPA — Smart Buoy (program khusus)");
  Serial.println("  FILL 40s → WAIT 60s → DRAIN 50s → JEDA 10s → ulang");
  Serial.println("══════════════════════════════════════════════");
}

void loop() {
  cycleCount++;
  Serial.printf("\n========== SIKLUS #%lu ==========", cycleCount);

  runPhase("FILLING",  true,  false, FILL_MS);   // 1. pompa ISI ON 40 dtk
  runPhase("WAITING",  false, false, WAIT_MS);   // 2. kedua OFF 60 dtk
  runPhase("DRAINING", false, true,  DRAIN_MS);  // 3. pompa BUANG ON 50 dtk
  runPhase("JEDA",     false, false, IDLE_MS);   // 4. kedua OFF 10 dtk
  // → loop() otomatis mengulang dari FILLING
}
