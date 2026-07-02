/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Edi Loupatty
 *
 * This file is part of the Smart Buoy IoT System.
 */

/**
 * @file receiver_gateway.ino
 * @brief NODE RECEIVER — terima LoRa → stempel waktu (NTP) → upload Firebase.
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │  UPLOAD SKETCH INI KE: ESP32 RECEIVER (di pos jaga, dekat WiFi). │
 * │  Hanya butuh ESP32 + modul LoRa. WiFi pakai bawaan ESP32.        │
 * └─────────────────────────────────────────────────────────────────┘
 *
 * Tugas node ini:
 *   - Dengarkan paket LoRa dari node BUOY
 *   - Beri timestamp dari NTP (buoy tidak punya WiFi/jam)
 *   - Upload ke Firebase RTDB:
 *       paket 'L' → /smart_buoy/live  (PUT)  + cache untuk /history
 *       paket 'P' → /smart_buoy/pump_status (PUT)
 *       batas 10 menit → /smart_buoy/history (POST, clock-aligned)
 *   - WiFi auto-reconnect
 *
 * Format paket LoRa yang diterima:
 *     SB|<type>|<pump>|<phaseMs>|<dbg>|<pH>|<temp>|<turb>
 */

#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "Config.h"

// ── Web server (monitoring via browser di IP ESP32) ──────────────────────────
WebServer server(80);

// ── NTP ──────────────────────────────────────────────────────────────────────
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 25200;  // WIB (GMT+7)
const int   daylightOffset_sec = 0;

// ── State telemetry terkini (dari paket LoRa terakhir) ───────────────────────
float  phValue     = 0;
float  tempC       = 0;
float  turbidity   = 0;
String kondisi     = "Jernih";
String pumpLabel   = "IDLE";
unsigned long pumpPhaseMs = 0;
int    debugMode   = 0;   // dikirim oleh buoy; menentukan apakah /history aktif

// ── Cache "air tenang" untuk history clock-aligned (paket type 'L') ──────────
float  lastGoodPH   = -100.0;
float  lastGoodTemp = -100.0;
float  lastGoodTurb = -100.0;
bool   hasGoodReading = false;

// Slot 10-menit terakhir yang sudah dikirim ke history (= epoch / HISTORY_INTERVAL_SEC).
long   lastHistorySlot = -1;

// ── Status untuk web interface ───────────────────────────────────────────────
bool          loraReady    = false;  // hasil init modul LoRa (true = radio terdeteksi)
int           lastRssi     = 0;      // RSSI paket LoRa terakhir (dBm)
unsigned long lastPacketMs = 0;      // millis() saat paket LoRa terakhir diterima (0 = belum ada)
unsigned long packetCount  = 0;      // total paket valid yang diterima sejak boot

// ── Status aktuator (di-overhear dari paket CMD buoy → aktuator) ─────────────
// Gateway tidak memerintah; ia hanya MENDENGAR perintah buoy yang lewat udara
// agar bisa menampilkan status pompa cooling & dispenser kapur ke web/Firebase.
bool          actCoolPump   = false;  // desired state pompa cooling (1/0) dari buoy
uint32_t      actDoseId     = 0;      // doseId kapur terkini yang terdengar
unsigned long actLastDoseMs = 0;      // millis() saat doseId terakhir naik (0 = belum)
unsigned long actLastCmdMs  = 0;      // millis() paket CMD terakhir terdengar (0 = belum)

// ── Forward declarations ─────────────────────────────────────────────────────
bool   initLoRa();
void   handlePacket(const String &msg);
void   handleCommandOverheard(const String &msg);
bool   sendControlStatus(const String &tsStr);
String getTurbidityStatus(float ntuValue);

bool   initWiFi();
void   serviceHistoryScheduler();

bool   sendLiveTelemetry(const String &tsStr);
bool   sendHistoryReading(float ph, float temp, float turb, const String &tsStr);
bool   sendPumpStatus(const String &tsStr);
bool   sendFirebasePUT(const String &path, const String &json);
bool   sendFirebasePOST(const String &path, const String &json);
bool   sendFirebaseHTTP(const String &path, const String &json, int method);

time_t   getEpochSeconds();
String   getTimestampString();
uint64_t getNetworkTimestamp();

void   initWebServer();
void   handleRoot();
void   handleData();

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n══════════════════════════════════════════");
  Serial.println("  Smart Buoy IoT — NODE RECEIVER (LoRa→WiFi)");
  Serial.println("══════════════════════════════════════════");

  loraReady = initLoRa();
  if (!loraReady) {
    Serial.println("[LoRa] ✗ Init GAGAL — cek wiring modul. Restart untuk coba lagi.");
  }

  if (!initWiFi()) {
    Serial.println("[WiFi] ✗ Skip — akan reconnect otomatis di loop.");
  }

  initWebServer();  // halaman monitoring di http://<IP ESP32>
}

// =============================================================================
// LOOP — terima LoRa + scheduler history + jaga WiFi
// =============================================================================
void loop() {
  static unsigned long lastWifiCheckAt = millis();

  // ── 1. Terima paket LoRa (polling) ──────────────────────────────────────────
  int packetSize = LoRa.parsePacket();
  if (packetSize > 0) {
    String msg = LoRa.readString();
    msg.trim();
    lastRssi = LoRa.packetRssi();
    Serial.printf("[LoRa RX] ← %s (RSSI %d dBm)\n", msg.c_str(), lastRssi);

    // Router: paket perintah (CMD|...) vs paket sensor (SB|...)
    if (msg.startsWith(LORA_CMD_PREFIX "|")) handleCommandOverheard(msg);
    else                                     handlePacket(msg);
  }

  // ── 2. Layani permintaan halaman web (monitoring) ───────────────────────────
  server.handleClient();

  // ── 3. Scheduler history CLOCK-ALIGNED ──────────────────────────────────────
  serviceHistoryScheduler();

  // ── 3. Jaga koneksi WiFi ────────────────────────────────────────────────────
  unsigned long now = millis();
  if (now - lastWifiCheckAt >= WIFI_RECHECK_INTERVAL_MS) {
    lastWifiCheckAt = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] ⚠ Koneksi terputus — mencoba reconnect...");
      initWiFi();
    }
  }

  delay(10);
}

// =============================================================================
// LoRa
// =============================================================================

/**
 * @brief Inisialisasi modul LoRa Ra-02 SX1278 dalam mode terima (polling).
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
  LoRa.enableCrc();  // paket korup otomatis dibuang

  Serial.printf("[LoRa] ✓ RX siap @ %.0f MHz | SF%d\n",
                (double)LORA_FREQUENCY / 1e6, LORA_SPREADING_FACTOR);
  return true;
}

/**
 * @brief Parse paket "SB|type|pump|phaseMs|dbg|pH|temp|turb" lalu teruskan ke Firebase.
 *
 *   type 'L' → update /live + cache "air tenang" untuk history
 *   type 'P' → update /pump_status
 */
void handlePacket(const String &msg) {
  // ── Split pipe-delimited menjadi 8 field ──
  const int N = 8;
  String parts[N];
  int idx = 0, start = 0;
  for (int i = 0; i <= (int)msg.length() && idx < N; i++) {
    if (i == (int)msg.length() || msg.charAt(i) == '|') {
      parts[idx++] = msg.substring(start, i);
      start = i + 1;
    }
  }

  // ── Validasi struktur paket ──
  if (idx != N || parts[0] != LORA_PREFIX) {
    Serial.println("[LoRa RX] ⚠ Paket tidak dikenali — diabaikan.");
    return;
  }

  char type = parts[1].length() ? parts[1].charAt(0) : '?';

  // Paket valid → catat untuk web interface
  lastPacketMs = millis();
  packetCount++;

  // ── Ekstrak nilai ──
  pumpLabel   = parts[2];
  pumpPhaseMs = strtoul(parts[3].c_str(), nullptr, 10);
  debugMode   = parts[4].toInt();
  phValue     = parts[5].toFloat();
  tempC       = parts[6].toFloat();
  turbidity   = parts[7].toFloat();
  kondisi     = getTurbidityStatus(turbidity);

  if (type == 'L') {
    // Sampel air tenang → cache untuk history + update /live
    lastGoodPH   = phValue;
    lastGoodTemp = tempC;
    lastGoodTurb = turbidity;
    hasGoodReading = true;

    if (WiFi.status() == WL_CONNECTED) {
      sendLiveTelemetry(getTimestampString());
    }
  } else if (type == 'P') {
    // Perubahan fase pompa → update /pump_status
    if (WiFi.status() == WL_CONNECTED) {
      sendPumpStatus(getTimestampString());
    }
  } else {
    Serial.printf("[LoRa RX] ⚠ Type tidak dikenal: '%c'\n", type);
  }
}

/**
 * @brief Parse paket perintah "CMD|dst|v1|v2|seq" yang dipancarkan buoy ke
 *        node aktuator. Gateway hanya MENDENGAR (tidak memerintah) untuk
 *        monitoring: update status + dorong ke /smart_buoy/control.
 *
 *   dst 'P' (pompa): v1 = state(1/0)
 *   dst 'K' (kapur): v1 = doseId  (naik = ada dosis baru)
 */
void handleCommandOverheard(const String &msg) {
  const int N = 5;
  String parts[N];
  int idx = 0, start = 0;
  for (int i = 0; i <= (int)msg.length() && idx < N; i++) {
    if (i == (int)msg.length() || msg.charAt(i) == '|') {
      parts[idx++] = msg.substring(start, i);
      start = i + 1;
    }
  }
  if (idx != N) {
    Serial.println("[CMD RX] ⚠ Paket perintah tidak lengkap — diabaikan.");
    return;
  }

  lastPacketMs = millis();   // paket valid juga menandakan link hidup
  actLastCmdMs = millis();
  char dst = parts[1].length() ? parts[1].charAt(0) : '?';

  if (dst == CMD_DST_PUMP) {
    actCoolPump = (parts[2].toInt() != 0);
  } else if (dst == CMD_DST_KAPUR) {
    uint32_t newDose = strtoul(parts[2].c_str(), nullptr, 10);
    if (newDose > actDoseId) {           // doseId naik = ada tabur kapur baru
      actDoseId     = newDose;
      actLastDoseMs = millis();
      Serial.printf("[CMD RX] Dosis kapur baru terdeteksi → doseId=%lu\n",
                    (unsigned long)actDoseId);
    } else {
      actDoseId = newDose;
    }
  } else {
    Serial.printf("[CMD RX] ⚠ Tujuan tidak dikenal: '%c'\n", dst);
    return;
  }

  if (WiFi.status() == WL_CONNECTED) sendControlStatus(getTimestampString());
}

/**
 * @brief Label kualitatif kejernihan dari NTU (mirror Sensors.cpp di buoy).
 */
String getTurbidityStatus(float ntuValue) {
  if (ntuValue < 25.0f)  return "Jernih";
  if (ntuValue < 100.0f) return "Keruh";
  return "Sangat Keruh";
}

// =============================================================================
// HISTORY SCHEDULER (clock-aligned, di sisi receiver karena di sini ada NTP)
// =============================================================================

/**
 * @brief Kirim /history TEPAT pada batas jam kelipatan HISTORY_INTERVAL_SEC,
 *        memakai sampel "air tenang" terakhir dari buoy. Timestamp dibulatkan
 *        ke batas slot (:00 / :10 / :20 ...) agar interval konstan (validitas DES).
 */
void serviceHistoryScheduler() {
  if (debugMode) return;                          // mode debug: cukup /live
  if (WiFi.status() != WL_CONNECTED) return;

  time_t epoch = getEpochSeconds();
  if ((uint64_t)epoch < NTP_VALID_EPOCH) return;  // NTP belum sync

  long slot = (long)(epoch / HISTORY_INTERVAL_SEC);

  // Boot pertama: catat slot saat ini sebagai acuan, jangan langsung kirim.
  if (lastHistorySlot < 0) {
    lastHistorySlot = slot;
    return;
  }

  if (slot == lastHistorySlot) return;            // masih dalam slot yang sama
  lastHistorySlot = slot;

  if (!hasGoodReading) {
    Serial.println("[History] ⚠ Batas 10-menit tiba tapi belum ada sampel air "
                   "tenang dari buoy — slot dilewati.");
    return;
  }

  uint64_t tsAlignedMs = (uint64_t)slot * HISTORY_INTERVAL_SEC * 1000ULL;
  char tsBuf[24];
  snprintf(tsBuf, sizeof(tsBuf), "%llu", tsAlignedMs);

  Serial.printf("[History] ▶ Batas 10-menit → kirim (ts=%s | Suhu=%.1f | pH=%.2f | Turb=%.1f)\n",
                tsBuf, lastGoodTemp, lastGoodPH, lastGoodTurb);
  sendHistoryReading(lastGoodPH, lastGoodTemp, lastGoodTurb, String(tsBuf));
}

// =============================================================================
// FIREBASE PAYLOAD BUILDERS
// =============================================================================

/** @brief /smart_buoy/live (PUT, overwrite). */
bool sendLiveTelemetry(const String &tsStr) {
  String json = "{\"pH\":" + String(phValue, 2) +
                ",\"temp\":" + String(tempC, 1) +
                ",\"turb\":" + String(turbidity, 1) +
                ",\"kondisi\":\"" + kondisi + "\"" +
                ",\"pump\":\"" + pumpLabel + "\"" +
                ",\"ts\":" + tsStr + "}";
  bool ok = sendFirebasePUT("/smart_buoy/live", json);
  Serial.println(ok ? "[Firebase Live] ✓ Terkirim" : "[Firebase Live] ✗ Gagal");
  return ok;
}

/** @brief /smart_buoy/history (POST, append). Pump dikunci "WAITING". */
bool sendHistoryReading(float ph, float temp, float turb, const String &tsStr) {
  String json = "{\"pH\":" + String(ph, 2) +
                ",\"temp\":" + String(temp, 1) +
                ",\"turb\":" + String(turb, 1) +
                ",\"kondisi\":\"" + getTurbidityStatus(turb) + "\"" +
                ",\"pump\":\"WAITING\"" +
                ",\"ts\":" + tsStr + "}";
  bool ok = sendFirebasePOST("/smart_buoy/history", json);
  Serial.println(ok ? "[Firebase History] ✓ Tersimpan" : "[Firebase History] ✗ Gagal");
  return ok;
}

/** @brief /smart_buoy/pump_status (PUT, overwrite). */
bool sendPumpStatus(const String &tsStr) {
  String json = "{\"state\":\"" + pumpLabel + "\"" +
                ",\"phaseDurationMs\":" + String(pumpPhaseMs) +
                ",\"debugMode\":" + String(debugMode) +
                ",\"ts\":" + tsStr + "}";
  return sendFirebasePUT("/smart_buoy/pump_status", json);
}

/** @brief /smart_buoy/control (PUT). Status aktuator hasil overhear paket CMD. */
bool sendControlStatus(const String &tsStr) {
  String json = "{\"coolPump\":\"" + String(actCoolPump ? "ON" : "OFF") + "\"" +
                ",\"doseId\":" + String((unsigned long)actDoseId) +
                ",\"ts\":" + tsStr + "}";
  bool ok = sendFirebasePUT("/smart_buoy/control", json);
  Serial.println(ok ? "[Firebase Control] ✓ Terkirim" : "[Firebase Control] ✗ Gagal");
  return ok;
}

// =============================================================================
// WIFI
// =============================================================================
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
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);  // init NTP
    return true;
  }
  Serial.println("\n[WiFi] ✗ Timeout.");
  return false;
}

// =============================================================================
// FIREBASE REST API (via WiFi HTTPS)
// =============================================================================
bool sendFirebasePUT(const String &path, const String &json) {
  return sendFirebaseHTTP(path, json, 1);
}

bool sendFirebasePOST(const String &path, const String &json) {
  return sendFirebaseHTTP(path, json, 2);
}

bool sendFirebaseHTTP(const String &path, const String &json, int method) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] ✗ WiFi tidak terhubung");
    return false;
  }

  for (int attempt = 1; attempt <= 2; attempt++) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15000);
    client.setHandshakeTimeout(15);

    HTTPClient http;
    http.setConnectTimeout(15000);
    http.setTimeout(15000);

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

    int code = 0;
    if (method == 1)      code = http.PUT(json);
    else if (method == 2) code = http.POST(json);

    if (code == 200 || code == 201) {
      http.end();
      return true;
    }

    Serial.printf("[HTTP] ✗ Attempt %d gagal, kode: %d\n", attempt, code);
    if (code > 0) Serial.printf("[HTTP] Respons: %s\n", http.getString().c_str());
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

/** @brief Epoch NTP dalam DETIK (ringan, dipanggil tiap loop oleh scheduler). */
time_t getEpochSeconds() {
  time_t now;
  time(&now);
  return now;
}

/** @brief Timestamp NTP saat ini sebagai String milidetik (untuk payload). */
String getTimestampString() {
  uint64_t timestamp = getNetworkTimestamp();
  char tsBuf[24];
  snprintf(tsBuf, sizeof(tsBuf), "%llu", timestamp);
  return String(tsBuf);
}

uint64_t getNetworkTimestamp() {
  time_t now;
  time(&now);

  if (now > (time_t)NTP_VALID_EPOCH) {
    return (uint64_t)now * 1000ULL;  // milidetik untuk kompatibilitas Flutter
  }

  // NTP belum sync — kembalikan 0 (slot history akan menunggu sync)
  Serial.println("[Time] ⚠ NTP belum sync.");
  return 0ULL;
}

// =============================================================================
// WEB INTERFACE — monitoring via browser di http://<IP ESP32>
// =============================================================================

/** @brief Daftarkan route & jalankan web server di port 80. */
void initWebServer() {
  server.on("/", handleRoot);
  server.on("/data", handleData);   // endpoint JSON untuk auto-refresh
  server.begin();
  Serial.print("[Web] ✓ Server aktif → buka http://");
  Serial.println(WiFi.localIP());
}

/** @brief Endpoint JSON: status LoRa + telemetry terkini (dipanggil AJAX tiap 2 dtk). */
void handleData() {
  long age = (lastPacketMs == 0) ? -1 : (long)(millis() - lastPacketMs);
  String j = "{";
  j += "\"lora\":";    j += (loraReady ? "true" : "false");
  j += ",\"rssi\":";   j += String(lastRssi);
  j += ",\"ageMs\":";  j += String(age);
  j += ",\"packets\":";j += String(packetCount);
  j += ",\"pH\":";     j += String(phValue, 2);
  j += ",\"temp\":";   j += String(tempC, 1);
  j += ",\"turb\":";   j += String(turbidity, 1);
  j += ",\"kondisi\":\""; j += kondisi;   j += "\"";
  j += ",\"pump\":\"";    j += pumpLabel; j += "\"";
  j += ",\"phaseMs\":";   j += String(pumpPhaseMs);
  j += ",\"debug\":";     j += String(debugMode);
  // Status aktuator (overhear dari paket CMD buoy)
  long doseAge = (actLastDoseMs == 0) ? -1 : (long)(millis() - actLastDoseMs);
  j += ",\"coolPump\":\"";j += (actCoolPump ? "ON" : "OFF"); j += "\"";
  j += ",\"doseId\":";    j += String((unsigned long)actDoseId);
  j += ",\"doseAgeMs\":"; j += String(doseAge);
  j += ",\"ip\":\"";      j += WiFi.localIP().toString(); j += "\"";
  j += "}";
  server.send(200, "application/json", j);
}

/** @brief Halaman utama (HTML+JS). Data diisi via fetch ke /data tiap 2 detik. */
void handleRoot() {
  static const char PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Smart Buoy — Receiver</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#0f172a;color:#e2e8f0;padding:16px}
  .wrap{max-width:560px;margin:0 auto}
  h1{font-size:1.25rem;margin-bottom:4px}
  .sub{color:#94a3b8;font-size:.8rem;margin-bottom:16px}
  .status{display:flex;align-items:center;gap:10px;padding:14px 16px;border-radius:12px;margin-bottom:10px;font-weight:600}
  .dot{width:12px;height:12px;border-radius:50%;flex:none}
  .ok{background:#064e3b;color:#6ee7b7}      .ok .dot{background:#34d399;box-shadow:0 0 8px #34d399}
  .warn{background:#451a03;color:#fcd34d}    .warn .dot{background:#fbbf24;box-shadow:0 0 8px #fbbf24}
  .bad{background:#450a0a;color:#fca5a5}     .bad .dot{background:#f87171;box-shadow:0 0 8px #f87171}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:14px}
  .card{background:#1e293b;border-radius:12px;padding:14px}
  .card .lbl{color:#94a3b8;font-size:.72rem;text-transform:uppercase;letter-spacing:.5px}
  .card .val{font-size:1.6rem;font-weight:700;margin-top:4px}
  .card .unit{font-size:.85rem;color:#94a3b8;font-weight:400}
  .meta{margin-top:14px;font-size:.78rem;color:#94a3b8;line-height:1.7}
  .pill{display:inline-block;background:#334155;color:#e2e8f0;border-radius:999px;padding:2px 10px;font-size:.75rem}
</style>
</head>
<body>
<div class="wrap">
  <h1>🛟 Smart Buoy — Receiver</h1>
  <div class="sub">Monitoring data LoRa dari node transmitter (buoy)</div>

  <div id="loraStatus" class="status warn"><span class="dot"></span><span id="loraText">Menghubungkan…</span></div>
  <div id="linkStatus" class="status warn"><span class="dot"></span><span id="linkText">Menunggu data…</span></div>

  <div class="grid">
    <div class="card"><div class="lbl">pH</div><div class="val" id="pH">--</div></div>
    <div class="card"><div class="lbl">Suhu</div><div class="val" id="temp">--<span class="unit"> °C</span></div></div>
    <div class="card"><div class="lbl">Turbidity</div><div class="val" id="turb">--<span class="unit"> NTU</span></div></div>
    <div class="card"><div class="lbl">Kondisi Air</div><div class="val" id="kondisi" style="font-size:1.2rem">--</div></div>
  </div>

  <div class="meta">
    Pompa sampling (buoy): <span class="pill" id="pump">--</span><br>
    Pompa cooling (suhu): <span class="pill" id="coolPump">--</span><br>
    Dispenser kapur: <span class="pill" id="dose">--</span><br>
    RSSI: <span id="rssi">--</span> dBm &nbsp;•&nbsp; Total paket: <span id="packets">0</span><br>
    Update terakhir: <span id="age">--</span><br>
    IP ESP32: <span id="ip">--</span>
  </div>
</div>

<script>
function setStatus(el,txtEl,cls,txt){el.className='status '+cls;txtEl.textContent=txt;}
async function tick(){
  try{
    const r = await fetch('/data',{cache:'no-store'});
    const d = await r.json();
    // Status modul LoRa
    if(d.lora) setStatus(loraStatus,loraText,'ok','✓ Terhubung ke modul LoRa');
    else       setStatus(loraStatus,loraText,'bad','✗ Modul LoRa GAGAL — cek wiring');
    // Status link data
    if(d.ageMs < 0)            setStatus(linkStatus,linkText,'warn','⏳ Belum ada data dari transmitter');
    else if(d.ageMs < 60000)   setStatus(linkStatus,linkText,'ok','📡 Menerima data dari transmitter');
    else                       setStatus(linkStatus,linkText,'warn','⚠ Data lama — transmitter mungkin offline');
    // Nilai sensor
    pH.textContent=d.pH; temp.innerHTML=d.temp+'<span class="unit"> °C</span>';
    turb.innerHTML=d.turb+'<span class="unit"> NTU</span>'; kondisi.textContent=d.kondisi;
    pump.textContent=d.pump; rssi.textContent=d.rssi; packets.textContent=d.packets; ip.textContent=d.ip;
    age.textContent = d.ageMs<0 ? 'belum ada' : (Math.round(d.ageMs/1000)+' detik lalu');
    // Aktuator
    coolPump.textContent = d.coolPump;
    dose.textContent = d.doseAgeMs<0 ? ('siaga (0 dosis)')
                       : ('dosis #'+d.doseId+' • '+Math.round(d.doseAgeMs/1000)+' detik lalu');
  }catch(e){
    setStatus(linkStatus,linkText,'bad','✗ Gagal mengambil data dari ESP');
  }
}
tick(); setInterval(tick,2000);
</script>
</body>
</html>)HTML";
  server.send_P(200, "text/html", PAGE);
}
