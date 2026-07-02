/*
 * @file debug_buoy_tx.ino
 * @brief Mock Buoy Transmitter — simulasi buoy asli untuk NODE POMPA + NODE KAPUR.
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │  UPLOAD KE: ESP8266 cadangan + modul LoRa SX1278                 │
 * └─────────────────────────────────────────────────────────────────┘
 *
 * Meniru perilaku buoy asli (sendActuatorCommands):
 *   - Tiap CMD_REBROADCAST_MS (15 detik) kirim DUA paket berurutan:
 *       CMD|P|<state>|0|<seq>                — state pompa cooling
 *       CMD|K|<doseId>|<openMs>|<seq>        — doseId kapur terkini
 *   - doseId yang SAMA di-rebroadcast terus (node kapur harus dedup,
 *     TIDAK menabur ulang). doseId hanya naik saat Anda perintahkan.
 *   - Karena paket P dan K selalu terkirim bersamaan, filter tujuan di
 *     kedua node ikut teruji (paket "bukan untukku" harus diabaikan).
 *
 * MODE AUTO (default saat boot — cocok ditenagai charger HP tanpa laptop):
 *   Pompa bersiklus otomatis: ON 15 detik → OFF 5 detik → ulang terus.
 *   Relay node pompa ikut berirama sama = bukti link hidup.
 *
 * Kontrol via Serial Monitor (baudrate 115200, kirim dengan Enter):
 *   a  → nyalakan/matikan mode AUTO
 *   1  → pompa ON  (otomatis pindah ke mode MANUAL)
 *   0  → pompa OFF (otomatis pindah ke mode MANUAL)
 *   t  → tabur 1 dosis kapur (doseId naik 1)
 *   s  → tampilkan status saat ini
 *
 * Uji fail-safe node pompa: saat pompa ON, cabut power alat ini lalu
 * tunggu ±90 detik — relay node pompa harus OFF sendiri.
 */

#include <SPI.h>
#include <LoRa.h>
#include "Config.h"

bool     pumpOn   = false; // state pompa cooling yang disimulasikan
bool     autoMode = true;  // boot langsung AUTO — bisa jalan tanpa Serial Monitor
uint32_t doseId   = 0;     // 0 = belum pernah dosis (node kapur sinkron tanpa menabur)
uint32_t seq      = 0;     // penghitung kirim, naik per paket (seperti buoy asli)

void sendActuatorCommands();
void printStatus();

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n══════════════════════════════════════════");
  Serial.println("  DEBUG BUOY TX — simulasi POMPA + KAPUR");
  Serial.println("══════════════════════════════════════════");

  SPI.begin();
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("[LoRa] ✗ Init GAGAL — cek wiring modul LoRa.");
    while (1);
  }

  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.enableCrc();

  Serial.println("[LoRa] ✓ TX siap.");
  Serial.println("\n[INFO] Perintah Serial Monitor (akhiri dengan Enter):");
  Serial.println("  a = auto ON/OFF | 1 = pompa ON | 0 = pompa OFF | t = dosis kapur | s = status");
  Serial.printf("[INFO] MODE AUTO aktif: pompa ON %lu dtk → OFF %lu dtk, berulang.\n\n",
                AUTO_PUMP_ON_MS / 1000UL, AUTO_PUMP_OFF_MS / 1000UL);

  pumpOn = true;            // mulai siklus AUTO dari fase NYALA
  sendActuatorCommands();
}

void loop() {
  // ── 1. Perintah dari Serial Monitor ──────────────────────────────────────
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "a" || cmd == "A") {
      autoMode = !autoMode;
      Serial.printf("[Kontrol] Mode AUTO → %s\n", autoMode ? "AKTIF" : "MATI");
    } else if (cmd == "1") {
      autoMode = false;
      pumpOn = true;
      Serial.println("[Kontrol] Pompa → ON (mode MANUAL)");
      sendActuatorCommands();
    } else if (cmd == "0") {
      autoMode = false;
      pumpOn = false;
      Serial.println("[Kontrol] Pompa → OFF (mode MANUAL)");
      sendActuatorCommands();
    } else if (cmd == "t" || cmd == "T") {
      doseId++;
      Serial.printf("[Kontrol] DOSIS kapur #%lu\n", (unsigned long)doseId);
      sendActuatorCommands();
    } else if (cmd == "s" || cmd == "S") {
      printStatus();
    } else if (cmd.length() > 0) {
      Serial.println("[Kontrol] ⚠ Perintah tidak dikenal (pakai a/1/0/t/s).");
    }
  }

  // ── 2. MODE AUTO: siklus pompa ON 15 dtk → OFF 5 dtk, berulang ───────────
  if (autoMode) {
    static unsigned long phaseStartedAt = 0;
    unsigned long phaseMs = pumpOn ? AUTO_PUMP_ON_MS : AUTO_PUMP_OFF_MS;
    if (millis() - phaseStartedAt >= phaseMs) {
      phaseStartedAt = millis();
      pumpOn = !pumpOn;
      Serial.printf("[Auto] Pompa → %s\n", pumpOn ? "ON (15 dtk)" : "OFF (5 dtk)");
      sendActuatorCommands();
    }
  }

  // ── 3. Rebroadcast berkala — persis pola buoy asli ───────────────────────
  static unsigned long lastCmdAt = 0;
  if (millis() - lastCmdAt >= CMD_REBROADCAST_MS) {
    lastCmdAt = millis();
    sendActuatorCommands();
  }

  delay(50);
}

/**
 * @brief Kirim paket P dan K berurutan — format sama persis dengan buoy asli.
 */
void sendActuatorCommands() {
  // Pompa cooling
  {
    String pkt = String(LORA_CMD_PREFIX) + "|" + String(CMD_DST_PUMP) + "|" +
                 String(pumpOn ? 1 : 0) + "|0|" + String(++seq);
    LoRa.beginPacket(); LoRa.print(pkt); int ok = LoRa.endPacket();
    Serial.printf("[LoRa CMD] %s → %s\n", ok ? "✓" : "✗", pkt.c_str());
  }

  // Dispenser kapur
  {
    String pkt = String(LORA_CMD_PREFIX) + "|" + String(CMD_DST_KAPUR) + "|" +
                 String((unsigned long)doseId) + "|" + String(DOSE_SERVO_OPEN_MS) +
                 "|" + String(++seq);
    LoRa.beginPacket(); LoRa.print(pkt); int ok = LoRa.endPacket();
    Serial.printf("[LoRa CMD] %s → %s\n", ok ? "✓" : "✗", pkt.c_str());
  }
}

void printStatus() {
  Serial.println("── STATUS ─────────────────────────────────");
  Serial.printf("  Mode   : %s\n", autoMode ? "AUTO" : "MANUAL");
  Serial.printf("  Pompa  : %s\n", pumpOn ? "ON" : "OFF");
  Serial.printf("  doseId : %lu\n", (unsigned long)doseId);
  Serial.printf("  seq    : %lu\n", (unsigned long)seq);
  Serial.println("───────────────────────────────────────────");
}
