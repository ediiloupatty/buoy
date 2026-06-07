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
RTC_DATA_ATTR float lastSentTurb = -100.0;  ///< Last turbidity (NTU) sent to Firebase

// Pump state machine — auto-cycle FILLING → WAITING → DRAINING → loop
// ESP32 always-on, mulai otomatis dari FILLING saat power on.
// Mobile app hanya monitoring (tidak ada command/button).
RTC_DATA_ATTR int pumpState = PUMP_IDLE;  // di-set ke FILLING di setup()

// Global Telemetry State (current reading)
float  phValue   = 0;
float  tempC     = 0;
float  turbidity = 0;           ///< Kekeruhan terkini (NTU)
String kondisi   = "Jernih";    ///< Label kualitatif kejernihan

// ── Cache "air tenang" untuk history clock-aligned ───────────────────────────
// Diisi saat fase WAITING (air sudah mengendap). Saat batas jam 10-menit tiba,
// nilai inilah yang dikirim ke /history — apa pun fase pompa saat itu.
float  lastGoodPH     = -100.0;  ///< pH air-tenang terakhir (cache)
float  lastGoodTemp   = -100.0;  ///< Suhu air-tenang terakhir (cache)
float  lastGoodTurb   = -100.0;  ///< Kekeruhan air-tenang terakhir (cache, NTU)
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
bool   sendHistoryReading(float ph, float temp, float turb, const String &tsStr);
void   serviceHistoryScheduler();
time_t getEpochSeconds();
String getTimestampString();

bool   initWiFi();

bool   sendFirebasePUT(const String &path, const String &json);
bool   sendFirebasePOST(const String &path, const String &json);
bool   sendFirebaseHTTP(const String &path, const String &json, int method);
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
  turbidity = readTurbidityNTU();
  kondisi   = getTurbidityStatus(turbidity);
  Serial.printf("[Sensor] Suhu=%.1f°C | pH=%.2f | Turb=%.1f NTU (%s)\n",
                tempC, phValue, turbidity, kondisi.c_str());

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
      tempC     = readTemperature();
      phValue   = readPH(tempC);
      turbidity = readTurbidityNTU();
      kondisi   = getTurbidityStatus(turbidity);
      lastGoodTemp   = tempC;
      lastGoodPH     = phValue;
      lastGoodTurb   = turbidity;
      hasGoodReading = true;
      Serial.printf("[Sensor] WAITING sample → Suhu=%.1f°C | pH=%.2f | Turb=%.1f NTU (%s) (cached)\n",
                    tempC, phValue, turbidity, kondisi.c_str());
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

  Serial.printf("[History] ▶ Batas 10-menit → kirim (ts=%s | Suhu=%.1f | pH=%.2f | Turb=%.1f)\n",
                tsBuf, lastGoodTemp, lastGoodPH, lastGoodTurb);
  sendHistoryReading(lastGoodPH, lastGoodTemp, lastGoodTurb, String(tsBuf));
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
                ",\"turb\":" + String(turbidity, 1) +
                ",\"kondisi\":\"" + kondisi + "\"" +
                ",\"pump\":\"" + String(currentPumpLabel()) + "\"" +
                ",\"ts\":" + tsStr + "}";
  bool ok = sendFirebasePUT("/smart_buoy/live", json);
  if (ok) {
    Serial.println("[Firebase Live] ✓ Terkirim");
    lastSentPH = phValue; lastSentTemp = tempC; lastSentTurb = turbidity;
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
bool sendHistoryReading(float ph, float temp, float turb, const String &tsStr) {
  String json = "{\"pH\":" + String(ph, 2) +
                ",\"temp\":" + String(temp, 1) +
                ",\"turb\":" + String(turb, 1) +
                ",\"kondisi\":\"" + getTurbidityStatus(turb) + "\"" +
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

// =============================================================================
// FIREBASE REST API (via WiFi HTTPS)
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