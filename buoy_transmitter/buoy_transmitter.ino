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
#include <Preferences.h>  // NVS — persist doseId agar tahan reboot

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

// ── KONTROL AKTUATOR (buoy = otak) ───────────────────────────────────────────
// Buoy mengevaluasi aturan saat dapat sampel air tenang, lalu mengirim paket
// CMD ke node aktuator ESP8266. Perintah dikirim sebagai "state yang diinginkan"
// dan DIULANG berkala (idempotent) — kalau 1 paket LoRa hilang, paket berikutnya
// otomatis membenahi. Khusus kapur memakai doseId agar 1 dosis = 1 kali tabur.
bool          coolPumpOn   = false;  ///< desired state pompa cooling (suhu)
uint32_t      doseId       = 0;      ///< id dosis kapur terkini (0 = belum pernah)
Preferences   dosePrefs;             ///< NVS handle — namespace "dose" (persist doseId)
unsigned long lastDoseMs   = 0;      ///< millis() dosis terakhir (untuk cooldown)
bool          everDosed    = false;  ///< sudah pernah dosis? (agar dosis pertama lolos cooldown)
int           dosesToday   = 0;      ///< jumlah dosis dalam jendela 24 jam berjalan
unsigned long dayWindowMs  = 0;      ///< awal jendela 24 jam (millis)

// ── MODE SIMULASI DEMO (dipicu perintah serial "si") ─────────────────────────
// Menjalankan skenario terskrip agar demo di aplikasi pasti terlihat: suhu naik
// → pompa nyala, lalu pH turun → kapur ditabur → pH pulih. Aktuator diperintah
// LANGSUNG (bukan lewat evaluateControl) supaya tidak bergantung ambang/histeresis.
bool          simActive    = false;  ///< true = skenario demo sedang berjalan
int           simStep      = 0;      ///< langkah skenario saat ini (0..6)
unsigned long simStepStart = 0;      ///< millis() saat langkah ini dimulai
unsigned long simLastTx    = 0;      ///< millis() TX SB|L terakhir saat simulasi
unsigned long simLastCmd   = 0;      ///< millis() broadcast CMD aktuator terakhir

// Durasi & nilai simulasi (buoy-only; tidak ditaruh di Config.h bersama node lain)
#define SIM_SEG_MS          60000UL  ///< 1 menit per segmen skenario
#define SIM_GAP_MS         120000UL  ///< 2 menit transisi suhu → pH
// SB|L (suhu/pH) SERING agar app cepat update; CMD aktuator JARANG (state tak
// berubah dalam 1 langkah) agar tidak berebut kanal & bikin SB|L terlewat.
#define SIM_LIVE_TX_MS       3000UL  ///< kirim SB|L (suhu/pH) tiap 3 detik
#define SIM_CMD_TX_MS       20000UL  ///< segarkan CMD pompa/kapur tiap 20 detik
#define SIM_TEMP_NORMAL      31.0f   ///< suhu aman (app: 26–32°C = Aman)
#define SIM_TEMP_HOT         32.5f   ///< suhu panas: >32°C (Bahaya) & ≥ TEMP_PUMP_ON_C, lonjakan kecil (+1,5°C)
#define SIM_PH_NORMAL         7.9f   ///< pH aman (app: 7,5–8,5 = Aman)
#define SIM_PH_LOW            7.1f   ///< pH turun: <7,5 (Bahaya) & ≤ PH_DOSE_BELOW
#define SIM_TURB_NORMAL      20.0f   ///< kekeruhan stabil (Jernih/Aman)

// ── Forward declarations ─────────────────────────────────────────────────────
void applyPumpRelay(int state);
unsigned long getPumpStateDuration(int state);
int nextPumpState(int state);
const char* currentPumpLabel();
const char* pumpStateLabel(int state);
bool initLoRa();
void sendLoRaPacket(char type);
void evaluateControl();
void sendActuatorCommands();
void startSimulation();
void serviceSimulation();
void finishSimulation();
void simSendLive();
void simForcePump(bool on);
void simForceDose();

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

  // 6. Muat doseId dari NVS — tanpa ini, reboot buoy me-reset doseId ke 0
  //    sementara node_kapur masih memegang lastDoseId lama → dosis diabaikan.
  dosePrefs.begin("dose", true);  // read-only
  doseId = dosePrefs.getUInt("id", 0);
  dosePrefs.end();
  if (doseId > 0) {
    // Konservatif: anggap dosis terakhir baru saja terjadi, supaya reboot
    // beruntun saat pH rendah tidak menabur ulang sebelum cooldown lewat.
    everDosed  = true;
    lastDoseMs = millis();
    Serial.printf("[Kontrol] doseId=%lu dipulihkan dari NVS (cooldown mulai ulang)\n",
                  (unsigned long)doseId);
  }

  // 7. Evaluasi kontrol awal + kirim perintah aktuator (state aman default)
  dayWindowMs = millis();
  evaluateControl();
  sendActuatorCommands();
}

// =============================================================================
// LOOP — siklus pompa otomatis + kirim LoRa
// =============================================================================
void loop() {
  static unsigned long stateStartedAt = millis();
  static unsigned long lastSampleAt   = 0;  // timer sampling saat WAITING

  unsigned long now     = millis();
  unsigned long elapsed = now - stateStartedAt;

  // ── 0. Perintah via Serial: "si" = mulai simulasi demo, sisanya = kalibrasi ──
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.equalsIgnoreCase("si")) startSimulation();
    else                            handleCalibrationCommand(cmd);
  }

  // ── 0b. Mode simulasi demo aktif → ambil alih, lewati siklus pompa normal ────
  if (simActive) {
    serviceSimulation();
    delay(50);
    return;
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

      // Sampel SEGAR → evaluasi aturan kontrol & langsung kirim perintah.
      evaluateControl();
      sendActuatorCommands();
    }
  }

  // ── 3. Re-broadcast perintah aktuator berkala (jaga link + fail-safe) ────────
  static unsigned long lastCmdAt = 0;
  if (now - lastCmdAt >= CMD_REBROADCAST_MS) {
    lastCmdAt = now;
    sendActuatorCommands();
  }

  delay(50);  // cegah tight loop
}

// =============================================================================
// CONTROL ENGINE — buoy mengevaluasi aturan suhu & pH
// =============================================================================

/**
 * @brief Evaluasi aturan kontrol berdasar pembacaan sensor terkini.
 *
 *  • Pompa cooling : histeresis suhu (ON ≥ TEMP_PUMP_ON_C, OFF ≤ TEMP_PUMP_OFF_C).
 *  • Dispenser kapur: bila pH ≤ PH_DOSE_BELOW DAN cooldown sudah lewat DAN
 *    belum melebihi DOSE_MAX_PER_DAY → naikkan doseId (memicu 1 tabur di node).
 *
 * Catatan: pembacaan suhu DS18B20 gagal = -127°C; pembacaan ini diabaikan agar
 * fault sensor tidak salah memicu aktuator.
 */
void evaluateControl() {
  unsigned long now = millis();

  // Reset jendela harian tiap 24 jam (rolling, tanpa butuh NTP).
  if (now - dayWindowMs >= 24UL * 60UL * 60UL * 1000UL) {
    dayWindowMs = now;
    dosesToday  = 0;
  }

  // ── Pompa cooling: histeresis suhu ──
  if (tempC > -126.0f) {  // abaikan fault DS18B20
    if (tempC >= TEMP_PUMP_ON_C)       coolPumpOn = true;
    else if (tempC <= TEMP_PUMP_OFF_C) coolPumpOn = false;
  }

  // ── Dispenser kapur: pH rendah + cooldown + batas harian ──
  if (phValue > 0.0f && phValue <= PH_DOSE_BELOW) {
    bool cooldownOK = (!everDosed) || (now - lastDoseMs >= DOSE_COOLDOWN_MS);
    if (cooldownOK && dosesToday < DOSE_MAX_PER_DAY) {
      doseId++;                 // memicu 1 dosis baru di node_kapur
      lastDoseMs = now;
      everDosed  = true;
      dosesToday++;

      // Persist ke NVS — max 8 tulis/hari, jauh di bawah batas wear flash.
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
 * @brief Kirim paket perintah ke node aktuator via LoRa.
 *
 * Format (pipe-delimited, 5 field):
 *     CMD|<dst>|<v1>|<v2>|<seq>
 *   dst 'P' (pompa): v1 = state(1/0), v2 = 0
 *   dst 'K' (kapur): v1 = doseId,     v2 = openMs (lama servo membuka)
 *   seq = penghitung kirim (untuk log/debug)
 */
void sendActuatorCommands() {
  if (!loraReady) return;
  static uint32_t seq = 0;

  // Pompa cooling
  {
    String pkt = String(LORA_CMD_PREFIX) + "|" + String(CMD_DST_PUMP) + "|" +
                 String(coolPumpOn ? 1 : 0) + "|0|" + String(++seq);
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

// =============================================================================
// MODE SIMULASI DEMO — perintah serial "si"
// =============================================================================
//
// Skenario terskrip (total ±7 menit), tiap segmen 1 menit kecuali transisi 2 menit:
//   Langkah 1 (60 dtk) : Suhu NORMAL (31°C)            → app: suhu Aman
//   Langkah 2 (60 dtk) : Suhu PANAS (32,5°C) + POMPA ON→ app: suhu Bahaya, pompa nyala
//   Langkah 3 (120 dtk): Suhu pulih normal, pompa OFF  → app: normal (transisi suhu→pH)
//   Langkah 4 (60 dtk) : pH NORMAL (7,9)               → app: pH Aman
//   Langkah 5 (60 dtk) : pH TURUN (7,1) + TABUR KAPUR  → app: pH Bahaya, kapur ditabur
//   Langkah 6 (60 dtk) : pH NAIK bertahap → normal     → app: pH kembali Aman
//   Selesai            : semua parameter NORMAL, kembali ke operasi biasa
//
// Aktuator (pompa & kapur) DIPERINTAH LANGSUNG, bukan lewat evaluateControl, agar
// pasti jalan tanpa bergantung ambang suhu/pH atau histeresis.

/** @brief Kirim SB|L dengan nilai simulasi terkini → update /live di aplikasi. */
void simSendLive() {
  sendLoRaPacket('L');
}

/** @brief Broadcast perintah aktuator 2× (andal lawan paket hilang) & catat waktunya. */
void simBroadcastCmd() {
  sendActuatorCommands();
  delay(150);              // beri jeda agar receiver selesai proses paket pertama
  sendActuatorCommands();
  simLastCmd = millis();
}

/** @brief Paksa state pompa cooling (tanpa evaluateControl) & broadcast CMD|P. */
void simForcePump(bool on) {
  coolPumpOn = on;
  simBroadcastCmd();       // CMD|P pakai coolPumpOn baru; CMD|K (doseId tetap) di-dedup node
  Serial.printf("[SIM] Pompa cooling dipaksa %s\n", on ? "ON" : "OFF");
}

/** @brief Paksa 1 dosis kapur (tanpa evaluateControl): naikkan doseId & broadcast CMD|K. */
void simForceDose() {
  doseId++;
  dosePrefs.begin("dose", false);
  dosePrefs.putUInt("id", doseId);
  dosePrefs.end();
  simBroadcastCmd();       // CMD|K doseId naik → node_kapur menabur 1 dosis
  Serial.printf("[SIM] Kapur dipaksa tabur → doseId=%lu\n", (unsigned long)doseId);
}

/** @brief Mulai / reset skenario simulasi demo. */
void startSimulation() {
  simActive    = true;
  simStep      = 1;
  simStepStart = millis();
  simLastTx    = 0;

  Serial.println("\n╔════════════════════════════════════════════╗");
  Serial.println("║   MODE SIMULASI DEMO DIMULAI (perintah si)  ║");
  Serial.println("╚════════════════════════════════════════════╝");
  Serial.println("[SIM] Langkah 1/6: Suhu NORMAL (60 dtk)");

  // Kondisi awal: semua normal, pompa OFF.
  tempC     = SIM_TEMP_NORMAL;
  phValue   = SIM_PH_NORMAL;
  turbidity = SIM_TURB_NORMAL;
  simForcePump(false);
  simSendLive();
}

/** @brief Bereskan simulasi: kembalikan semua ke normal & aman, lanjut operasi biasa. */
void finishSimulation() {
  tempC     = SIM_TEMP_NORMAL;
  phValue   = SIM_PH_NORMAL;
  turbidity = SIM_TURB_NORMAL;
  simForcePump(false);   // pastikan pompa OFF
  simSendLive();         // kirim status normal terakhir
  simActive = false;
  Serial.println("[SIM] ✓ Simulasi selesai — semua parameter NORMAL. Kembali ke operasi biasa.");
}

/** @brief State machine skenario demo (dipanggil tiap loop saat simActive). */
void serviceSimulation() {
  unsigned long now     = millis();
  unsigned long elapsed = now - simStepStart;
  unsigned long dur     = (simStep == 3) ? SIM_GAP_MS : SIM_SEG_MS;

  // ── Transisi ke langkah berikutnya ──
  if (elapsed >= dur) {
    simStep++;
    simStepStart = now;
    simLastTx    = 0;   // paksa TX segera di langkah baru

    switch (simStep) {
      case 2:  // Suhu panas + pompa ON
        Serial.println("[SIM] Langkah 2/6: Suhu PANAS 32,5°C (>32) → POMPA ON (60 dtk)");
        tempC = SIM_TEMP_HOT;
        simForcePump(true);
        break;
      case 3:  // Transisi: pulih normal, pompa OFF (2 menit)
        Serial.println("[SIM] Langkah 3/6: Suhu pulih NORMAL, pompa OFF — transisi (120 dtk)");
        tempC   = SIM_TEMP_NORMAL;
        phValue = SIM_PH_NORMAL;
        simForcePump(false);
        break;
      case 4:  // pH normal
        Serial.println("[SIM] Langkah 4/6: pH NORMAL 7,9 (60 dtk)");
        phValue = SIM_PH_NORMAL;
        break;
      case 5:  // pH turun + tabur kapur
        Serial.println("[SIM] Langkah 5/6: pH TURUN 7,1 (<7,5) → TABUR KAPUR (60 dtk)");
        phValue = SIM_PH_LOW;
        simForceDose();
        break;
      case 6:  // pH naik bertahap (dilakukan di badan langkah)
        Serial.println("[SIM] Langkah 6/6: pH NAIK pulih normal (60 dtk)");
        break;
      default: // selesai
        finishSimulation();
        return;
    }
    simSendLive();  // kirim segera saat masuk langkah baru
    return;
  }

  // ── Langkah 6: naikkan pH bertahap dari LOW → NORMAL (efek kapur mulai bekerja) ──
  if (simStep == 6) {
    float frac = (float)elapsed / (float)SIM_SEG_MS;         // 0..1
    phValue = SIM_PH_LOW + (SIM_PH_NORMAL - SIM_PH_LOW) * frac;
  }

  // ── SB|L (suhu/pH) SERING → /live di app cepat update mengikuti nilai simulasi ──
  if (simLastTx == 0 || (now - simLastTx >= SIM_LIVE_TX_MS)) {
    simLastTx = now;
    simSendLive();
  }

  // ── CMD aktuator JARANG (state tak berubah dalam 1 langkah) → tak berebut dgn SB|L ──
  if (now - simLastCmd >= SIM_CMD_TX_MS) {
    sendActuatorCommands();  // segarkan CMD|P (pompa) & CMD|K (dedup) lawan paket hilang
    simLastCmd = now;
  }
}
