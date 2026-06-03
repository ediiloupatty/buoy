/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Edi Loupatty
 *
 * This file is part of the Smart Buoy IoT System.
 */

/**
 * @file final_project.ino
 * @brief Core application logic for the Smart Buoy IoT System.
 *
 * Execution Model — AUTO-CYCLE, ALWAYS-ON:
 *   - Power-on → setup() init hardware, langsung mulai siklus FILLING
 *   - loop() jalan terus dengan state machine 3 fase:
 *       FILLING (30s) → WAITING (10m) → DRAINING (30s) → FILLING → ...
 *   - Pompa 1 (ISI) & Pompa 2 (BUANG) interlocked — tidak pernah ON bersamaan
 *   - Saat WAITING: sensor di-sampling berkala, nilai "air tenang" di-cache;
 *     /live diperbarui tiap sampling
 *   - /history CLOCK-ALIGNED: dikirim TEPAT di batas jam kelipatan 10 menit
 *     (:00, :10, :20 ...) memakai cache air-tenang, lepas dari siklus pompa
 *   - WiFi auto-reconnect kalau terputus
 *   - Mobile app HANYA untuk monitoring (read /live, /pump_status) — tidak ada command
 *
 * Note: pH sensor calibration di sketch terpisah (kalibrasi/).
 */

#include <time.h>
#include "driver/rtc_io.h"  // gpio_hold_en/dis untuk kunci state relay saat deep sleep

// Custom Hardware Modules
#include "Config.h"
#include "Sensors.h"

// ── Platform-specific includes ───────────────────────────────────────────────
// #include <HardwareSerial.h>
// // SIM800L Serial (UART2)
// HardwareSerial sim800(2);

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// NTP Settings
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 25200; // WIB (GMT+7)
const int   daylightOffset_sec = 0;

// ── RTC Memory: Survives deep sleep ──────────────────────────────────────────
RTC_DATA_ATTR int   bootCount = 0;          ///< Tracks number of wake cycles
RTC_DATA_ATTR float lastSentPH   = -100.0;  ///< Last pH value sent to Firebase
RTC_DATA_ATTR float lastSentTemp = -100.0;  ///< Last temperature sent to Firebase

// Pump state machine — auto-cycle FILLING → WAITING → DRAINING → loop
// ESP32 always-on, mulai otomatis dari FILLING saat power on.
// Mobile app hanya monitoring (tidak ada command/button).
RTC_DATA_ATTR int pumpState = PUMP_IDLE;  // di-set ke FILLING di setup()

// Global Telemetry State (current reading)
float  phValue   = 0;
float  tempC     = 0;

// ── Cache "air tenang" untuk history clock-aligned ───────────────────────────
// Diisi saat fase WAITING (air sudah mengendap). Saat batas jam 10-menit tiba,
// nilai inilah yang dikirim ke /history — apa pun fase pompa saat itu.
float  lastGoodPH     = -100.0;  ///< pH air-tenang terakhir (cache)
float  lastGoodTemp   = -100.0;  ///< Suhu air-tenang terakhir (cache)
bool   hasGoodReading = false;   ///< true setelah minimal 1x sampling WAITING

// Slot 10-menit terakhir yang sudah dikirim ke history (= epoch / HISTORY_INTERVAL_SEC).
// Survives deep sleep agar tidak dobel kirim pada slot yang sama setelah reset.
RTC_DATA_ATTR long lastHistorySlot = -1;

// ── Forward Declarations ─────────────────────────────────────────────────────
void applyPumpRelay(int state);
unsigned long getPumpStateDuration(int state);
int nextPumpState(int state);
const char* currentPumpLabel();
const char* pumpStateLabel(int state);
bool   sendPumpStatus(const String &tsStr);
bool   sendLiveTelemetry(const String &tsStr);
bool   sendHistoryReading(float ph, float temp, const String &tsStr);
void   serviceHistoryScheduler();
time_t getEpochSeconds();
String getTimestampString();

// bool   initSIM800L();
// void   resetSIM800L();
// bool   openGPRS();
// void   closeGPRS();
bool   initWiFi();

bool   sendFirebasePUT(const String &path, const String &json);
bool   sendFirebasePOST(const String &path, const String &json);
bool   sendFirebaseHTTP(const String &path, const String &json, int method);
// String sendAT(const String &cmd, unsigned long timeoutMs = 2000);
// bool   waitForResponse(const String &expected, unsigned long timeoutMs = 2000);
uint64_t getNetworkTimestamp();

// =============================================================================
// SETUP — Main execution path (runs on every wake from deep sleep)
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  bootCount++;
  Serial.println("\n══════════════════════════════════════════");
  Serial.printf("  Smart Buoy IoT — Boot #%d\n", bootCount);
  Serial.println("  [MODE: Auto-Cycle Always-On]");
  Serial.println("══════════════════════════════════════════");

  // 1. Initialize Hardware
  initSensors();
  loadCalibration();

  // 2. Init pin relay & langsung set fase awal: FILLING (pompa 1 ON, pompa 2 OFF)
  pinMode(PUMP_FILL_PIN,  OUTPUT);
  pinMode(PUMP_DRAIN_PIN, OUTPUT);
  pumpState = PUMP_FILLING;
  applyPumpRelay(pumpState);
  Serial.printf("[Pump] Siklus mulai — fase awal: %s\n", currentPumpLabel());

  // 3. Baca sensor pertama
  tempC     = readTemperature();
  phValue   = readPH(tempC);
  Serial.printf("[Sensor] Suhu=%.1f°C | pH=%.2f\n", tempC, phValue);

  // 4. Connect WiFi (non-blocking — kalau gagal, pompa tetap jalan)
  if (initWiFi()) {
    // Hanya kirim status pompa awal — data sensor (live/history) menyusul saat fase WAITING
    String tsStr = getTimestampString();
    sendPumpStatus(tsStr);
  } else {
    Serial.println("[WiFi] ✗ Skip telemetry awal, pompa tetap jalan otomatis.");
  }

  // loop() akan handle siklus & telemetry selanjutnya secara berkelanjutan
}

// =============================================================================
// LOOP — Siklus pompa otomatis (always-on)
// =============================================================================
/**
 * @brief State machine pompa otomatis berbasis millis() timing.
 *
 * Siklus berjalan terus-menerus tanpa intervensi user (durasi bebas):
 *   FILLING → WAITING → DRAINING → FILLING → ...
 *
 * Setiap iterasi:
 *   1. Cek transisi fase berdasarkan elapsed time (millis)
 *   2. Saat WAITING: sampling sensor berkala → cache "air tenang" + update /live
 *   3. Scheduler history clock-aligned (kirim di batas jam 10-menit via NTP)
 *   4. Cek & reconnect WiFi kalau lost
 *
 * Mobile app hanya read /smart_buoy/live & /pump_status — tidak ada command.
 */
void loop() {
  static unsigned long stateStartedAt  = millis();
  static unsigned long lastWifiCheckAt = millis();
  static unsigned long lastSampleAt    = 0;  // timer sampling sensor saat WAITING

  unsigned long now     = millis();
  unsigned long elapsed = now - stateStartedAt;

  // ── 1. Transisi fase pompa (siklus BEBAS, tidak terkait jadwal history) ──────
  unsigned long duration = getPumpStateDuration(pumpState);
  if (elapsed >= duration) {
    int prevState = pumpState;
    pumpState = nextPumpState(pumpState);
    stateStartedAt = now;
    applyPumpRelay(pumpState);
    Serial.printf("[Pump] %s → %s (setelah %lu ms)\n",
                  pumpStateLabel(prevState), pumpStateLabel(pumpState), elapsed);

    // Masuk WAITING → paksa sampling pertama (setelah endap) dimulai ulang
    if (pumpState == PUMP_WAITING) {
      lastSampleAt = 0;
    }

    // Kirim status pompa langsung saat transisi (app update real-time)
    if (WiFi.status() == WL_CONNECTED) {
      sendPumpStatus(getTimestampString());
    }
  }

  // ── 2. WAITING: endapkan → sampling berkala → cache "air tenang" + /live ─────
  //   Pembacaan di-cache ke lastGood{PH,Temp}; inilah yang nanti dikirim ke
  //   history pada batas jam 10-menit. /live diperbarui tiap sampling.
  if (pumpState == PUMP_WAITING && elapsed >= WAIT_SETTLE_MS) {
    if (lastSampleAt == 0 || (now - lastSampleAt >= WAIT_SAMPLE_INTERVAL_MS)) {
      lastSampleAt = now;
      tempC   = readTemperature();
      phValue = readPH(tempC);
      lastGoodTemp   = tempC;
      lastGoodPH     = phValue;
      hasGoodReading = true;
      Serial.printf("[Sensor] WAITING sample → Suhu=%.1f°C | pH=%.2f (cached)\n",
                    tempC, phValue);
      if (WiFi.status() == WL_CONNECTED) {
        sendLiveTelemetry(getTimestampString());
      }
    }
  }

  // ── 3. Scheduler history CLOCK-ALIGNED (epoch % HISTORY_INTERVAL_SEC) ────────
  serviceHistoryScheduler();

  // ── 4. Periodic cek WiFi & reconnect kalau lost ─────────────────────────────
  if (now - lastWifiCheckAt >= WIFI_RECHECK_INTERVAL_MS) {
    lastWifiCheckAt = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] ⚠ Koneksi terputus — mencoba reconnect...");
      initWiFi();  // non-blocking attempt
    }
  }

  delay(50);  // Cegah tight loop, hemat CPU
}

/**
 * @brief Kirim /history TEPAT pada batas jam kelipatan HISTORY_INTERVAL_SEC.
 *
 * Lepas dari siklus pompa. Memakai pembacaan "air tenang" terakhir (cache dari
 * fase WAITING). Timestamp DIBULATKAN ke batas slot — mis. menit :00, :10, :20 —
 * sehingga data history selalu rapi di kelipatan 10 menit & interval konstan
 * (penting untuk validitas DES di aplikasi).
 *
 * Terikat NTP → otomatis tidak drift. Bila NTP belum sync, slot dilewati.
 */
void serviceHistoryScheduler() {
  // Mode debug: JANGAN kirim /history (cukup /live). History hanya saat produksi.
  if (PUMP_DEBUG_MODE) return;

  if (WiFi.status() != WL_CONNECTED) return;

  time_t epoch = getEpochSeconds();
  if ((uint64_t)epoch < NTP_VALID_EPOCH) return;   // NTP belum sync → tunggu

  long slot = (long)(epoch / HISTORY_INTERVAL_SEC);

  // Boot pertama: jangan langsung kirim, cukup catat slot saat ini sebagai acuan.
  if (lastHistorySlot < 0) {
    lastHistorySlot = slot;
    return;
  }

  if (slot == lastHistorySlot) return;   // masih dalam slot 10-menit yang sama
  lastHistorySlot = slot;                // tandai slot ini sudah ditangani

  if (!hasGoodReading) {
    Serial.println("[History] ⚠ Batas 10-menit tiba tapi belum ada pembacaan "
                   "air tenang — slot dilewati.");
    return;
  }

  // Timestamp dibulatkan ke batas slot (ms) — selaras :00 / :10 / :20 ...
  uint64_t tsAlignedMs = (uint64_t)slot * HISTORY_INTERVAL_SEC * 1000ULL;
  char tsBuf[24];
  snprintf(tsBuf, sizeof(tsBuf), "%llu", tsAlignedMs);

  Serial.printf("[History] ▶ Batas 10-menit → kirim (ts=%s | Suhu=%.1f | pH=%.2f)\n",
                tsBuf, lastGoodTemp, lastGoodPH);
  sendHistoryReading(lastGoodPH, lastGoodTemp, String(tsBuf));
}

// =============================================================================
// PUMP CONTROL — State machine 3 fase otomatis (FILLING → WAITING → DRAINING)
// =============================================================================

/**
 * @brief Apply state pompa ke pin GPIO relay.
 *
 * INTERLOCK SAFETY: pompa 1 dan 2 tidak pernah ON bersamaan.
 *   FILLING  → pompa ISI ON, BUANG OFF
 *   DRAINING → pompa ISI OFF, BUANG ON
 *   WAITING/IDLE → kedua pompa OFF
 */
void applyPumpRelay(int state) {
  switch (state) {
    case PUMP_FILLING:
      digitalWrite(PUMP_DRAIN_PIN, RELAY_OFF);  // matikan dulu BUANG (interlock)
      digitalWrite(PUMP_FILL_PIN,  RELAY_ON);
      break;
    case PUMP_DRAINING:
      digitalWrite(PUMP_FILL_PIN,  RELAY_OFF);  // matikan dulu ISI (interlock)
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

/**
 * @brief Durasi (ms) dari setiap fase pompa.
 */
unsigned long getPumpStateDuration(int state) {
  switch (state) {
    case PUMP_FILLING:  return PUMP_FILL_DURATION_MS;
    case PUMP_WAITING:  return PUMP_WAIT_DURATION_MS;
    case PUMP_DRAINING: return PUMP_DRAIN_DURATION_MS;
    default:            return 0;
  }
}

/**
 * @brief Urutan transisi fase: FILLING → WAITING → DRAINING → FILLING (loop).
 */
int nextPumpState(int state) {
  switch (state) {
    case PUMP_FILLING:  return PUMP_WAITING;
    case PUMP_WAITING:  return PUMP_DRAINING;
    case PUMP_DRAINING: return PUMP_FILLING;
    default:            return PUMP_FILLING;  // safety: balik ke siklus normal
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

/**
 * @brief Helper: format current NTP timestamp jadi String milliseconds.
 */
String getTimestampString() {
  uint64_t timestamp = getNetworkTimestamp();
  char tsBuf[24];
  snprintf(tsBuf, sizeof(tsBuf), "%llu", timestamp);
  return String(tsBuf);
}

/**
 * @brief Kirim payload live telemetry ke /smart_buoy/live (PUT, overwrite).
 */
bool sendLiveTelemetry(const String &tsStr) {
  String json = "{\"pH\":" + String(phValue, 2) +
                ",\"temp\":" + String(tempC, 1) +
                ",\"pump\":\"" + String(currentPumpLabel()) + "\"" +
                ",\"ts\":" + tsStr + "}";
  bool ok = sendFirebasePUT("/smart_buoy/live", json);
  if (ok) {
    Serial.println("[Firebase Live] ✓ Terkirim");
    lastSentPH = phValue; lastSentTemp = tempC;
  } else {
    Serial.println("[Firebase Live] ✗ Gagal");
  }
  return ok;
}

/**
 * @brief Kirim payload history ke /smart_buoy/history (POST, append).
 *
 * Nilai pH/suhu diberikan eksplisit (pembacaan "air tenang" yang di-cache),
 * bukan dari global current — agar history selalu mencerminkan kondisi WAITING
 * walau pengiriman terjadi di fase pompa lain. Field "pump" dikunci "WAITING".
 */
bool sendHistoryReading(float ph, float temp, const String &tsStr) {
  String json = "{\"pH\":" + String(ph, 2) +
                ",\"temp\":" + String(temp, 1) +
                ",\"pump\":\"WAITING\"" +
                ",\"ts\":" + tsStr + "}";
  bool ok = sendFirebasePOST("/smart_buoy/history", json);
  if (ok) Serial.println("[Firebase History] ✓ Tersimpan");
  else    Serial.println("[Firebase History] ✗ Gagal");
  return ok;
}

/**
 * @brief Kirim status pompa ke Firebase untuk ditampilkan di mobile app.
 * Path: /smart_buoy/pump_status — overwrite (PUT) tiap transisi atau telemetry cycle.
 */
bool sendPumpStatus(const String &tsStr) {
  unsigned long durationMs = getPumpStateDuration(pumpState);
  String json = "{\"state\":\"" + String(currentPumpLabel()) + "\"" +
                ",\"phaseDurationMs\":" + String(durationMs) +
                ",\"debugMode\":" + String(PUMP_DEBUG_MODE) +
                ",\"ts\":" + tsStr + "}";
  return sendFirebasePUT("/smart_buoy/pump_status", json);
}

// =============================================================================
// ██╗    ██╗██╗███████╗██╗    ███╗   ███╗ ██████╗ ██████╗ ███████╗
// ██║    ██║██║██╔════╝██║    ████╗ ████║██╔═══██╗██╔══██╗██╔════╝
// ██║ █╗ ██║██║█████╗  ██║    ██╔████╔██║██║   ██║██║  ██║█████╗
// ██║███╗██║██║██╔══╝  ██║    ██║╚██╔╝██║██║   ██║██║  ██║██╔══╝
// ╚███╔███╔╝██║██║     ██║    ██║ ╚═╝ ██║╚██████╔╝██████╔╝███████╗
//  ╚══╝╚══╝ ╚═╝╚═╝     ╚═╝    ╚═╝     ╚═╝ ╚═════╝ ╚═════╝ ╚══════╝
// =============================================================================
// =============================================================================
// ███████╗██╗███╗   ███╗ █████╗  ██████╗  ██████╗ ██╗
// ██╔════╝██║████╗ ████║██╔══██╗██╔═████╗██╔═████╗██║
// ███████╗██║██╔████╔██║╚█████╔╝██║██╔██║██║██╔██║██║
// ╚════██║██║██║╚██╔╝██║██╔══██╗████╔╝██║████╔╝██║██║
// ███████║██║██║ ╚═╝ ██║╚█████╔╝╚██████╔╝╚██████╔╝███████╗
// ╚══════╝╚═╝╚═╝     ╚═╝ ╚════╝  ╚═════╝  ╚═════╝ ╚══════╝
// =============================================================================

/**
 * @brief Initializes WiFi connection
 */
bool initWiFi() {
  Serial.print("[WiFi] Menghubungkan ke ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] ✓ Terhubung.");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Init NTP time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    return true;
  } else {
    Serial.println("\n[WiFi] ✗ Timeout.");
    return false;
  }
}

// /**
//  * @brief Mereset SIM800L melalui pin RST (active LOW).
//  * Pull RST ke LOW selama 200ms, lalu release HIGH.
//  * Tunggu 3 detik agar modem selesai boot ulang.
//  */
// void resetSIM800L() {
//   pinMode(SIM_RST, OUTPUT);
//   digitalWrite(SIM_RST, HIGH);  // Pastikan HIGH dulu (idle state)
//   delay(100);
//   digitalWrite(SIM_RST, LOW);   // Trigger reset (active LOW)
//   delay(200);                   // Minimum pulse 100-200ms
//   digitalWrite(SIM_RST, HIGH);  // Release
//   delay(3000);                  // Tunggu modem boot ulang
//   Serial.println("[SIM800L] RST triggered — menunggu modem boot...");
// }

// /**
//  * @brief Initializes the SIM800L modem and verifies communication.
//  *
//  * Flow:
//  *  1. Start serial at default 9600 baud
//  *  2. Try AT command — jika tidak respons, lakukan PWRKEY toggle (power on)
//  *  3. Tunggu modem siap, nonaktifkan echo
//  *  4. Validasi SIM card dan registrasi jaringan
//  *
//  * @return true if modem responds, SIM detected, and network registered.
//  */
// bool initSIM800L() {
//   // ── Step 1: Start Serial ────────────────────────────────────────────────────
//   sim800.begin(SIM_BAUD, SERIAL_8N1, SIM_RX, SIM_TX);
//   delay(500);
// 
//   Serial.print("[SIM800L] Inisialisasi modem");
// 
//   // ── Step 2: Cek apakah modem sudah aktif (setelah wake dari deep sleep) ────
//   bool modemReady = false;
//   for (int i = 0; i < 3; i++) {
//     Serial.print(".");
//     String resp = sendAT("AT", 1500);
//     if (resp.indexOf("OK") != -1) {
//       modemReady = true;
//       break;
//     }
//     delay(500);
//   }
// 
//   // ── Step 3: Jika tidak respons, lakukan RST reset ──────────────────────────
//   if (!modemReady) {
//     Serial.println("\n[SIM800L] ⚠ Tidak ada respons — mencoba RST reset...");
//     resetSIM800L();
// 
//     // Coba lagi setelah power cycle (max 5 detik)
//     for (int i = 0; i < 5; i++) {
//       Serial.print(".");
//       String resp = sendAT("AT", 2000);
//       if (resp.indexOf("OK") != -1) {
//         modemReady = true;
//         break;
//       }
//       delay(1000);
//     }
//   }
// 
//   if (!modemReady) {
//     Serial.println("[SIM800L] ✗ Modem tidak merespons setelah RST reset.");
//     Serial.println("[SIM800L] ✗ Periksa: kabel TX/RX, tegangan power (4.6-5.2V untuk V2), dan sambungan RST.");
//     return false;
//   }
// 
//   Serial.println("\n[SIM800L] ✓ Modem merespons.");
// 
//   // ── Step 4: Konfigurasi dasar modem ─────────────────────────────────────────
//   sendAT("ATE0");          // Nonaktifkan echo
//   sendAT("AT+CMEE=2");     // Enable verbose error reporting (untuk debug)
// 
//   // ── Step 5: Validasi SIM Card ────────────────────────────────────────────────
//   String simResp = "";
//   for (int i = 0; i < 5; i++) {
//     simResp = sendAT("AT+CPIN?", 3000);
//     if (simResp.indexOf("READY") != -1) break;
//     delay(1000);
//   }
// 
//   if (simResp.indexOf("READY") != -1) {
//     Serial.println("[SIM800L] ✓ SIM Card terdeteksi.");
//   } else if (simResp.indexOf("SIM PIN") != -1) {
//     Serial.println("[SIM800L] ✗ SIM Card terkunci PIN — masukkan PIN dulu.");
//     return false;
//   } else if (simResp.indexOf("NO SIM") != -1 || simResp.indexOf("SIM not") != -1) {
//     Serial.println("[SIM800L] ✗ SIM Card tidak terpasang.");
//     return false;
//   } else {
//     Serial.printf("[SIM800L] ✗ Status SIM tidak dikenal: %s\n", simResp.c_str());
//     return false;
//   }
// 
//   // ── Step 6: Tunggu Registrasi Jaringan (max 45 detik) ───────────────────────
//   Serial.print("[SIM800L] Menunggu registrasi jaringan");
//   for (int j = 0; j < 45; j++) {
//     String creg = sendAT("AT+CREG?", 2000);
//     // +CREG: 0,1 = home network, +CREG: 0,5 = roaming
//     if (creg.indexOf(",1") != -1 || creg.indexOf(",5") != -1) {
//       Serial.println(" ✓ Terdaftar.");
// 
//       // Tampilkan info sinyal dan operator
//       String csq = sendAT("AT+CSQ", 1000);
//       String cops = sendAT("AT+COPS?", 2000);
//       Serial.printf("[SIM800L] Sinyal: %s\n", csq.c_str());
//       Serial.printf("[SIM800L] Operator: %s\n", cops.c_str());
//       return true;
//     }
//     Serial.print(".");
//     delay(1000);
//   }
// 
//   Serial.println("\n[SIM800L] ✗ Timeout registrasi jaringan.");
//   Serial.println("[SIM800L] ✗ Pastikan sinyal operator tersedia di lokasi ini.");
//   return false;
// }
// 
// /**
//  * @brief Opens a GPRS data connection using the configured APN.
//  * @return true if GPRS connection established successfully.
//  */
// bool openGPRS() {
//   Serial.print("[GPRS] Membuka koneksi");
// 
//   // Close any existing bearer (ignore errors)
//   sendAT("AT+SAPBR=0,1", 3000);
//   delay(500);
// 
//   // Configure bearer
//   sendAT("AT+SAPBR=3,1,\"Contype\",\"GPRS\"");
//   sendAT("AT+SAPBR=3,1,\"APN\",\"" + String(APN) + "\"");
// 
//   // Open bearer
//   String resp = sendAT("AT+SAPBR=1,1", 10000);
//   if (resp.indexOf("OK") != -1 || resp.indexOf("ERROR") != -1) {
//     // Check if bearer is actually open
//     delay(500);
//     String status = sendAT("AT+SAPBR=2,1", 3000);
//     if (status.indexOf("1,1,") != -1) {
//       Serial.println(" ✓ Terhubung.");
//       return true;
//     }
//   }
// 
//   // Retry once
//   delay(2000);
//   resp = sendAT("AT+SAPBR=1,1", 10000);
//   delay(500);
//   String status = sendAT("AT+SAPBR=2,1", 3000);
//   if (status.indexOf("1,1,") != -1) {
//       Serial.println(" ✓ Terhubung (retry).");
//       return true;
//   }
// 
//   Serial.println(" ✗ Gagal.");
//   return false;
// }
// 
// /**
//  * @brief Closes the active GPRS data connection.
//  */
// void closeGPRS() {
//   sendAT("AT+SAPBR=0,1", 3000);
//   Serial.println("[GPRS] Koneksi ditutup.");
// }

// =============================================================================
// FIREBASE REST API (via SIM800L HTTPS)
// =============================================================================

/**
 * @brief Sends a PUT request to Firebase RTDB (overwrites data at path).
 * Used for the /live endpoint.
 */
bool sendFirebasePUT(const String &path, const String &json) {
  return sendFirebaseHTTP(path, json, 1);
}

/**
 * @brief Sends a POST request to Firebase RTDB (appends new node at path).
 * Used for the /history endpoint.
 */
bool sendFirebasePOST(const String &path, const String &json) {
  return sendFirebaseHTTP(path, json, 2);
}

/**
 * @brief Core HTTP request handler using ESP32 HTTPClient.
 * @param path Firebase RTDB path (e.g., "/smart_buoy/live")
 * @param json JSON payload string
 * @param method 1 = PUT, 2 = POST
 * @return true if HTTP 200 response received
 */
bool sendFirebaseHTTP(const String &path, const String &json, int method) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] ✗ WiFi tidak terhubung");
    return false;
  }

  // Retry up to 2 times if HTTPS handshake fails (common on slow networks)
  for (int attempt = 1; attempt <= 2; attempt++) {
    WiFiClientSecure client;
    client.setInsecure();           // Skip SSL cert verification
    client.setTimeout(15000);       // 15 seconds for socket operations
    client.setHandshakeTimeout(15); // 15 seconds for TLS handshake

    HTTPClient http;
    http.setConnectTimeout(15000);  // 15 seconds to connect
    http.setTimeout(15000);          // 15 seconds for the whole request

    String url = "https://";
    url += FIREBASE_HOST;
    url += path;
    url += ".json?auth=";
    url += FIREBASE_AUTH;

    if (!http.begin(client, url)) {
      Serial.printf("[HTTP] ✗ Attempt %d: begin() gagal\n", attempt);
      http.end();
      delay(1000);
      continue;
    }
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = 0;
    if (method == 1) {
      httpResponseCode = http.PUT(json);
    } else if (method == 2) {
      httpResponseCode = http.POST(json);
    }

    if (httpResponseCode == 200 || httpResponseCode == 201) {
      http.end();
      return true;
    }

    Serial.printf("[HTTP] ✗ Attempt %d gagal, kode: %d\n", attempt, httpResponseCode);
    if (httpResponseCode > 0) {
      // Got a response but not success — print server message
      String payload = http.getString();
      Serial.printf("[HTTP] Respons: %s\n", payload.c_str());
    }
    http.end();

    if (attempt < 2) {
      Serial.println("[HTTP] ↻ Retry dalam 2 detik...");
      delay(2000);
    }
  }

  return false;
}

// /**
//  * @brief Sends a PUT request to Firebase RTDB (overwrites data at path).
//  * Used for the /live endpoint.
//  *
//  * SIM800L AT+HTTPACTION hanya support: 0=GET, 1=POST, 2=HEAD.
//  * Tidak ada native PUT. Firebase REST API memperlakukan POST ke path
//  * sebagai push (auto-key), KECUALI path diakhiri dengan node spesifik.
//  * Workaround: gunakan HTTPACTION=1 (POST) tapi bedakan di path.
//  * Untuk /live, Firebase Rules bisa di-set agar POST = overwrite,
//  * atau gunakan Firebase Legacy REST dengan ?x-http-method-override=PUT.
//  *
//  * Solusi terbaik: tambahkan header X-HTTP-Method-Override
//  */
// // bool sendFirebasePUT(const String &path, const String &json) {
// //   return sendFirebaseHTTP(path, json, 1);  // method=1: gunakan header override PUT
// // }
// 
// /**
//  * @brief Sends a POST request to Firebase RTDB (appends new node at path).
//  * Used for the /history endpoint.
//  */
// // bool sendFirebasePOST(const String &path, const String &json) {
// //   return sendFirebaseHTTP(path, json, 2);  // method=2: POST biasa
// // }
// 
// /**
//  * @brief Core HTTP request handler using SIM800L's built-in HTTP stack.
//  * @param path Firebase RTDB path (e.g., "/smart_buoy/live")
//  * @param json JSON payload string
//  * @param method 1 = PUT, 2 = POST
//  * @return true if HTTP 200 response received
//  */
// // bool sendFirebaseHTTP(const String &path, const String &json, int method) {
// //   // Build the full Firebase REST URL
// //   String url = "https://";
// //   url += FIREBASE_HOST;
// //   url += path;
// //   url += ".json?auth=";
// //   url += FIREBASE_AUTH;
// // 
// //   // Initialize HTTP service
// //   String resp = sendAT("AT+HTTPINIT", 3000);
// //   if (resp.indexOf("OK") == -1) {
// //     // HTTP might already be initialized, terminate and retry
// //     sendAT("AT+HTTPTERM", 1000);
// //     delay(500);
// //     resp = sendAT("AT+HTTPINIT", 3000);
// //     if (resp.indexOf("OK") == -1) return false;
// //   }
// // 
// //   // Configure HTTP parameters
// //   sendAT("AT+HTTPPARA=\"CID\",1");
// //   sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 3000);
// //   sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
// //   // Tambah header X-HTTP-Method-Override untuk PUT (SIM800L tidak support native PUT)
// //   if (method == 1) {
// //     sendAT("AT+HTTPPARA=\"USERDATA\",\"X-HTTP-Method-Override: PUT\\r\\n\"");
// //   }
// //   sendAT("AT+HTTPSSL=1");
// // 
// //   // Prepare data payload
// //   int dataLen = json.length();
// //   String dataCmd = "AT+HTTPDATA=" + String(dataLen) + ",10000";
// //   resp = sendAT(dataCmd, 5000);
// // 
// //   if (resp.indexOf("DOWNLOAD") != -1) {
// //     // SIM800L is ready to receive data
// //     sim800.print(json);
// //     delay(1000);
// //     waitForResponse("OK", 5000);
// //   } else {
// //     sendAT("AT+HTTPTERM", 1000);
// //     return false;
// //   }
// // 
// //   // Execute HTTP action
// //   // For PUT: SIM800L doesn't have native PUT, we use method 1 (GET-like workaround)
// //   // Firebase REST API: PUT = write, POST = push
// //   // SIM800L AT+HTTPACTION: 0=GET, 1=POST, 2=HEAD
// //   // Since SIM800L only supports GET/POST/HEAD, we use POST for both
// //   // Firebase treats POST as push (generates unique key) and PUT as set
// //   // Workaround: For PUT, append .json to path — Firebase REST API
// //   // Actually, we'll use HTTPACTION=1 (POST) for both, but handle path differently
// //   
// //   resp = sendAT("AT+HTTPACTION=1", 15000);  // POST request
// // 
// //   // Wait for +HTTPACTION response with status code
// //   delay(2000);
// //   
// //   // Read any pending response
// //   String actionResp = "";
// //   unsigned long start = millis();
// //   while (millis() - start < 10000) {
// //     while (sim800.available()) {
// //       char c = sim800.read();
// //       actionResp += c;
// //     }
// //     if (actionResp.indexOf("+HTTPACTION:") != -1) break;
// //     delay(100);
// //   }
// // 
// //   // Parse HTTP status code from +HTTPACTION: <method>,<status>,<datalen>
// //   bool success = false;
// //   if (actionResp.indexOf("200") != -1 || actionResp.indexOf("201") != -1) {
// //     success = true;
// //   } else {
// //     Serial.printf("[HTTP] Response: %s\n", actionResp.c_str());
// //   }
// // 
// //   // Terminate HTTP session
// //   sendAT("AT+HTTPTERM", 1000);
// // 
// //   return success;
// // }

// =============================================================================
// NETWORK TIMESTAMP (via NTP)
// =============================================================================

/**
 * @brief Retrieves Unix timestamp from NTP in MILLISECONDS.
 * Falls back to boot count estimation if network time unavailable.
 * @return Unix epoch timestamp in milliseconds (uint64_t), matches Flutter expectation.
 */
/**
 * @brief Epoch NTP dalam DETIK, ringan & tanpa delay (dipanggil tiap loop).
 *
 * Dipakai scheduler history untuk mendeteksi batas slot 10-menit. Mengembalikan
 * apa adanya dari time(); pemanggil membandingkan dengan NTP_VALID_EPOCH untuk
 * memastikan NTP sudah sync.
 */
time_t getEpochSeconds() {
  time_t now;
  time(&now);
  return now;
}

uint64_t getNetworkTimestamp() {
  // Use time(NULL) directly — safer than getLocalTime() which can trigger
  // LWIP thread safety assertion in newer ESP32 cores.
  // configTime() was called in initWiFi(); time() returns whatever NTP synced so far.
  // Give NTP a small grace period to sync after WiFi connect.
  delay(500);

  time_t now;
  time(&now);

  // Sanity check: if NTP synced, time will be post-2020 epoch (>1577836800)
  if (now > 1577836800) {
    // Return milliseconds (Unix epoch × 1000) for Flutter compatibility
    // Use 64-bit to avoid overflow (epoch_ms exceeds 2^32 since 1970)
    return (uint64_t)now * 1000ULL;
  }

  // Fallback: NTP not synced yet, estimate using boot count × sleep interval
  Serial.println("[Time] ⚠ NTP belum sync — gunakan estimasi.");
  return (uint64_t)bootCount * (SLEEP_DURATION_US / 1000ULL);
}

// // =============================================================================
// // NETWORK TIMESTAMP (via SIM800L)
// // =============================================================================
// 
// /**
//  * @brief Retrieves Unix timestamp from SIM800L's network time.
//  * Falls back to boot count estimation if network time unavailable.
//  * @return Unix epoch timestamp (unsigned long)
//  */
// // unsigned long getNetworkTimestamp() {
// //   // Request network time from SIM800L
// //   // AT+CCLK? returns +CCLK: "yy/MM/dd,HH:mm:ss±zz"
// //   String resp = sendAT("AT+CCLK?", 2000);
// // 
// //   int idx = resp.indexOf("+CCLK: \"");
// //   if (idx != -1) {
// //     // Parse the time string: "yy/MM/dd,HH:mm:ss+zz"
// //     String timeStr = resp.substring(idx + 8);
// //     int endIdx = timeStr.indexOf("\"");
// //     if (endIdx != -1) {
// //       timeStr = timeStr.substring(0, endIdx);
// // 
// //       // Parse components: yy/MM/dd,HH:mm:ss
// //       int year   = timeStr.substring(0, 2).toInt() + 2000;
// //       int month  = timeStr.substring(3, 5).toInt();
// //       int day    = timeStr.substring(6, 8).toInt();
// //       int hour   = timeStr.substring(9, 11).toInt();
// //       int minute = timeStr.substring(12, 14).toInt();
// //       int second = timeStr.substring(15, 17).toInt();
// // 
// //       // Convert to Unix timestamp using struct tm
// //       struct tm t;
// //       t.tm_year = year - 1900;
// //       t.tm_mon  = month - 1;
// //       t.tm_mday = day;
// //       t.tm_hour = hour;
// //       t.tm_min  = minute;
// //       t.tm_sec  = second;
// //       t.tm_isdst = 0;
// // 
// //       unsigned long ts = mktime(&t);
// //       if (ts > 1600000000) {  // Sanity check: after Sep 2020
// //         return ts;
// //       }
// //     }
// //   }
// // 
// //   // Fallback: estimate based on boot count (2 minutes per boot)
// //   Serial.println("[Time] ⚠ Gagal mendapatkan waktu jaringan — gunakan estimasi.");
// //   return (unsigned long)(bootCount * (SLEEP_DURATION_US / 1000000ULL));
// // }
// 
// // =============================================================================
// // AT COMMAND UTILITIES
// // =============================================================================
// 
// /**
//  * @brief Sends an AT command to SIM800L and returns the response.
//  * @param cmd The AT command string to send.
//  * @param timeoutMs Maximum time to wait for response (default: 2000ms).
//  * @return Full response string from the modem.
//  */
// // String sendAT(const String &cmd, unsigned long timeoutMs) {
// //   // Flush any pending data
// //   while (sim800.available()) sim800.read();
// // 
// //   sim800.println(cmd);
// // 
// //   String response = "";
// //   unsigned long start = millis();
// // 
// //   while (millis() - start < timeoutMs) {
// //     while (sim800.available()) {
// //       char c = sim800.read();
// //       response += c;
// //     }
// //     // Check for terminal responses
// //     if (response.indexOf("OK") != -1 ||
// //         response.indexOf("ERROR") != -1 ||
// //         response.indexOf("DOWNLOAD") != -1) {
// //       break;
// //     }
// //     delay(10);
// //   }
// // 
// //   return response;
// // }
// 
// /**
//  * @brief Waits for a specific response string from SIM800L.
//  * @param expected The expected substring in the response.
//  * @param timeoutMs Maximum time to wait.
//  * @return true if expected string found within timeout.
//  */
// // bool waitForResponse(const String &expected, unsigned long timeoutMs) {
// //   String response = "";
// //   unsigned long start = millis();
// // 
// //   while (millis() - start < timeoutMs) {
// //     while (sim800.available()) {
// //       char c = sim800.read();
// //       response += c;
// //     }
// //     if (response.indexOf(expected) != -1) return true;
// //     delay(10);
// //   }
// // 
// //   return false;
// // }