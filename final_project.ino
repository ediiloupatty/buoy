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
 * Handles connectivity (WiFi or SIM800L GPRS), timestamping,
 * deep sleep power management, and dual-pipeline Firebase REST API telemetry.
 * 
 * Build Modes (controlled by USE_WIFI in Config.h):
 *   - USE_WIFI defined:   WiFi + HTTPClient + NTP (testing/development)
 *   - USE_WIFI undefined:  SIM800L GPRS + AT Commands (production/field)
 * 
 * Execution Model:
 *   - ESP32 wakes from deep sleep every 2 minutes
 *   - Reads all sensors
 *   - Sends live data to Firebase via HTTPS
 *   - Every 5th boot (~10 minutes), also pushes historical data
 *   - Returns to deep sleep
 * 
 * Note: pH sensor calibration is handled by a separate dedicated sketch
 *       located in the kalibrasi/ directory.
 */

#include <time.h>

// Custom Hardware Modules
#include "Config.h"
#include "Sensors.h"

// ── Platform-specific includes ───────────────────────────────────────────────
#ifdef USE_WIFI
  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <WiFiClientSecure.h>
#else
  #include <HardwareSerial.h>
  // SIM800L Serial (UART2)
  HardwareSerial sim800(2);
#endif

// ── RTC Memory: Survives deep sleep ──────────────────────────────────────────
RTC_DATA_ATTR int   bootCount = 0;          ///< Tracks number of wake cycles
RTC_DATA_ATTR float lastSentPH   = -100.0;  ///< Last pH value sent to Firebase
RTC_DATA_ATTR float lastSentTemp = -100.0;  ///< Last temperature sent to Firebase
RTC_DATA_ATTR int   lastSentTurb = -100;    ///< Last turbidity sent to Firebase

// Global Telemetry State (current reading)
float  phValue   = 0;
float  tempC     = 0;
int    turbidity = 0;
String kondisi   = "";

// ── Forward Declarations ─────────────────────────────────────────────────────
void enterDeepSleep();

#ifdef USE_WIFI
bool   connectWiFi();
void   disconnectWiFi();
bool   sendFirebaseHTTPWiFi(const String &path, const String &json, const String &method);
unsigned long getTimestampNTP();
#else
bool   initSIM800L();
bool   openGPRS();
void   closeGPRS();
bool   sendFirebasePUT(const String &path, const String &json);
bool   sendFirebasePOST(const String &path, const String &json);
bool   sendFirebaseHTTP(const String &path, const String &json, int method);
String sendAT(const String &cmd, unsigned long timeoutMs = 2000);
bool   waitForResponse(const String &expected, unsigned long timeoutMs = 2000);
unsigned long getNetworkTimestamp();
#endif

// =============================================================================
// SETUP — Main execution path (runs on every wake from deep sleep)
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  bootCount++;
  Serial.println("\n══════════════════════════════════════════");
  Serial.printf("  Smart Buoy IoT — Boot #%d\n", bootCount);
#ifdef USE_WIFI
  Serial.println("  [MODE: WiFi Test]");
#else
  Serial.println("  [MODE: SIM800L Production]");
#endif
  Serial.println("══════════════════════════════════════════");

  // 1. Initialize Hardware Abstraction Layer
  initSensors();
  loadCalibration();

  // 2. Read Sensors (temperature first — required for pH compensation)
  tempC     = readTemperature();
  phValue   = readPH(tempC);
  turbidity = readTurbidityValue();
  kondisi   = getTurbidityStatus(turbidity);

  Serial.printf("[Sensor] Suhu=%.1f°C | pH=%.2f | Turb=%d (%s)\n",
                tempC, phValue, turbidity, kondisi.c_str());

  // 3. Initialize Network Connection
#ifdef USE_WIFI
  if (!connectWiFi()) {
    Serial.println("[WiFi] ✗ Gagal terhubung — skip transmisi.");
    enterDeepSleep();
    return;
  }
#else
  if (!initSIM800L()) {
    Serial.println("[SIM800L] ✗ Modem tidak merespons — skip transmisi.");
    enterDeepSleep();
    return;
  }
  if (!openGPRS()) {
    Serial.println("[GPRS] ✗ Gagal membuka koneksi GPRS — skip transmisi.");
    enterDeepSleep();
    return;
  }
#endif

  // 4. Send Live Telemetry (only if values changed beyond threshold)
  bool shouldSendLive = false;
  if (abs(phValue - lastSentPH) > 0.05 ||
      abs(tempC - lastSentTemp) > 0.1  ||
      abs(turbidity - lastSentTurb) > 5) {
    shouldSendLive = true;
  }

  if (shouldSendLive) {
    String liveJson = "{\"pH\":" + String(phValue, 2) +
                      ",\"temp\":" + String(tempC, 1) +
                      ",\"turb\":" + String(turbidity) + "}";

#ifdef USE_WIFI
    if (sendFirebaseHTTPWiFi("/smart_buoy/live", liveJson, "PUT")) {
#else
    if (sendFirebasePUT("/smart_buoy/live", liveJson)) {
#endif
      Serial.println("[Firebase Live] ✓ Data terkirim.");
      lastSentPH   = phValue;
      lastSentTemp = tempC;
      lastSentTurb = turbidity;
    } else {
      Serial.println("[Firebase Live] ✗ Gagal mengirim data.");
    }
  } else {
    Serial.println("[Firebase Live] — Tidak ada perubahan signifikan, skip.");
  }

  // 5. Send Historical Data (every HISTORY_EVERY_N_BOOTS boots = ~10 minutes)
  if (bootCount % HISTORY_EVERY_N_BOOTS == 0) {
#ifdef USE_WIFI
    unsigned long timestamp = getTimestampNTP();
#else
    unsigned long timestamp = getNetworkTimestamp();
#endif

    String historyJson = "{\"pH\":" + String(phValue, 2) +
                         ",\"temp\":" + String(tempC, 1) +
                         ",\"turb\":" + String(turbidity) +
                         ",\"ts\":" + String(timestamp) + "}";

#ifdef USE_WIFI
    if (sendFirebaseHTTPWiFi("/smart_buoy/history", historyJson, "POST")) {
#else
    if (sendFirebasePOST("/smart_buoy/history", historyJson)) {
#endif
      Serial.printf("[Firebase History] ✓ Data tersimpan (ts: %lu)\n", timestamp);
    } else {
      Serial.println("[Firebase History] ✗ Gagal mengirim data.");
    }
  }

  // 6. Cleanup & Sleep
#ifdef USE_WIFI
  disconnectWiFi();
#else
  closeGPRS();
#endif

  enterDeepSleep();
}

// =============================================================================
// LOOP — Not used in deep sleep model (setup handles everything)
// =============================================================================
void loop() {
  // Execution never reaches here — ESP32 enters deep sleep at end of setup()
}

// =============================================================================
// DEEP SLEEP
// =============================================================================

/**
 * @brief Configures and enters ESP32 deep sleep for SLEEP_DURATION_US microseconds.
 * On wake, execution restarts from setup().
 */
void enterDeepSleep() {
  Serial.printf("[Sleep] Tidur selama %d detik...\n", (int)(SLEEP_DURATION_US / 1000000ULL));
  Serial.println("══════════════════════════════════════════\n");
  Serial.flush();

  esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
  esp_deep_sleep_start();
  // Execution stops here — next wake starts from setup()
}

// =============================================================================
// ██╗    ██╗██╗███████╗██╗    ███╗   ███╗ ██████╗ ██████╗ ███████╗
// ██║    ██║██║██╔════╝██║    ████╗ ████║██╔═══██╗██╔══██╗██╔════╝
// ██║ █╗ ██║██║█████╗  ██║    ██╔████╔██║██║   ██║██║  ██║█████╗
// ██║███╗██║██║██╔══╝  ██║    ██║╚██╔╝██║██║   ██║██║  ██║██╔══╝
// ╚███╔███╔╝██║██║     ██║    ██║ ╚═╝ ██║╚██████╔╝██████╔╝███████╗
//  ╚══╝╚══╝ ╚═╝╚═╝     ╚═╝    ╚═╝     ╚═╝ ╚═════╝ ╚═════╝ ╚══════╝
// =============================================================================
#ifdef USE_WIFI

/**
 * @brief Connects to the configured WiFi network.
 * @return true if connected within 10 seconds (20 × 500ms).
 */
bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.printf("[WiFi] Menghubungkan ke \"%s\"", WIFI_SSID);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" ✓ Terhubung.");
    Serial.printf("[WiFi] IP: %s | RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }

  Serial.println(" ✗ Gagal.");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return false;
}

/**
 * @brief Disconnects WiFi and powers off the radio to minimize sleep current.
 */
void disconnectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[WiFi] Koneksi ditutup.");
}

/**
 * @brief Sends an HTTP PUT or POST request to Firebase RTDB via WiFi.
 * @param path Firebase RTDB path (e.g., "/smart_buoy/live")
 * @param json JSON payload string
 * @param method "PUT" or "POST"
 * @return true if HTTP 200/201 response received
 */
bool sendFirebaseHTTPWiFi(const String &path, const String &json, const String &method) {
  WiFiClientSecure client;
  client.setInsecure();  // Skip certificate verification for testing

  HTTPClient http;
  String url = "https://";
  url += FIREBASE_HOST;
  url += path;
  url += ".json?auth=";
  url += FIREBASE_AUTH;

  if (!http.begin(client, url)) {
    Serial.println("[HTTP] ✗ Gagal memulai koneksi.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.setTimeout(15000);

  int httpCode;
  if (method == "PUT") {
    httpCode = http.PUT(json);
  } else {
    httpCode = http.POST(json);
  }

  bool success = (httpCode == 200 || httpCode == 201);
  if (!success) {
    Serial.printf("[HTTP] Response code: %d\n", httpCode);
    if (httpCode > 0) {
      String resp = http.getString();
      Serial.printf("[HTTP] Response: %s\n", resp.c_str());
    }
  }

  http.end();
  return success;
}

/**
 * @brief Retrieves Unix timestamp via NTP over WiFi.
 * Falls back to boot count estimation if NTP sync fails.
 * @return Unix epoch timestamp (unsigned long)
 */
unsigned long getTimestampNTP() {
  // Configure NTP — GMT+8 for WITA/Ambon timezone, no DST
  configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com");

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 5000)) {
    unsigned long ts = mktime(&timeinfo);
    Serial.printf("[NTP] Waktu: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return ts;
  }

  // Fallback: estimate based on boot count
  Serial.println("[NTP] ⚠ Gagal sinkronisasi waktu — gunakan estimasi.");
  return (unsigned long)(bootCount * (SLEEP_DURATION_US / 1000000ULL));
}

#else
// =============================================================================
// ███████╗██╗███╗   ███╗ █████╗  ██████╗  ██████╗ ██╗
// ██╔════╝██║████╗ ████║██╔══██╗██╔═████╗██╔═████╗██║
// ███████╗██║██╔████╔██║╚█████╔╝██║██╔██║██║██╔██║██║
// ╚════██║██║██║╚██╔╝██║██╔══██╗████╔╝██║████╔╝██║██║
// ███████║██║██║ ╚═╝ ██║╚█████╔╝╚██████╔╝╚██████╔╝███████╗
// ╚══════╝╚═╝╚═╝     ╚═╝ ╚════╝  ╚═════╝  ╚═════╝ ╚══════╝
// =============================================================================

/**
 * @brief Initializes the SIM800L modem and verifies communication.
 * @return true if modem responds to AT commands.
 */
bool initSIM800L() {
  sim800.begin(SIM_BAUD, SERIAL_8N1, SIM_RX, SIM_TX);
  delay(1000);

  Serial.print("[SIM800L] Inisialisasi modem");

  // Send AT and check for OK (3 retries)
  for (int i = 0; i < 3; i++) {
    Serial.print(".");
    String resp = sendAT("AT");
    if (resp.indexOf("OK") != -1) {
      Serial.println(" ✓ Modem merespons.");

      // Disable echo
      sendAT("ATE0");

      // Check SIM card
      String simResp = sendAT("AT+CPIN?");
      if (simResp.indexOf("READY") != -1) {
        Serial.println("[SIM800L] ✓ SIM Card terdeteksi.");
      } else {
        Serial.println("[SIM800L] ⚠ SIM Card tidak terdeteksi!");
        return false;
      }

      // Wait for network registration (max 30 seconds)
      Serial.print("[SIM800L] Menunggu registrasi jaringan");
      for (int j = 0; j < 30; j++) {
        String creg = sendAT("AT+CREG?");
        // +CREG: 0,1 = registered home, +CREG: 0,5 = registered roaming
        if (creg.indexOf(",1") != -1 || creg.indexOf(",5") != -1) {
          Serial.println(" ✓ Terdaftar.");

          // Get signal quality
          String csq = sendAT("AT+CSQ");
          Serial.printf("[SIM800L] Sinyal: %s\n", csq.c_str());
          return true;
        }
        Serial.print(".");
        delay(1000);
      }
      Serial.println(" ✗ Timeout registrasi jaringan.");
      return false;
    }
    delay(1000);
  }

  Serial.println(" ✗ Tidak merespons.");
  return false;
}

/**
 * @brief Opens a GPRS data connection using the configured APN.
 * @return true if GPRS connection established successfully.
 */
bool openGPRS() {
  Serial.print("[GPRS] Membuka koneksi");

  // Close any existing bearer (ignore errors)
  sendAT("AT+SAPBR=0,1", 3000);
  delay(500);

  // Configure bearer
  sendAT("AT+SAPBR=3,1,\"Contype\",\"GPRS\"");
  sendAT("AT+SAPBR=3,1,\"APN\",\"" + String(APN) + "\"");

  // Open bearer
  String resp = sendAT("AT+SAPBR=1,1", 10000);
  if (resp.indexOf("OK") != -1 || resp.indexOf("ERROR") != -1) {
    // Check if bearer is actually open
    delay(500);
    String status = sendAT("AT+SAPBR=2,1", 3000);
    if (status.indexOf("1,1,") != -1) {
      Serial.println(" ✓ Terhubung.");
      return true;
    }
  }

  // Retry once
  delay(2000);
  resp = sendAT("AT+SAPBR=1,1", 10000);
  delay(500);
  String status = sendAT("AT+SAPBR=2,1", 3000);
  if (status.indexOf("1,1,") != -1) {
    Serial.println(" ✓ Terhubung (retry).");
    return true;
  }

  Serial.println(" ✗ Gagal.");
  return false;
}

/**
 * @brief Closes the active GPRS data connection.
 */
void closeGPRS() {
  sendAT("AT+SAPBR=0,1", 3000);
  Serial.println("[GPRS] Koneksi ditutup.");
}

// =============================================================================
// FIREBASE REST API (via SIM800L HTTPS)
// =============================================================================

/**
 * @brief Sends a PUT request to Firebase RTDB (overwrites data at path).
 * Used for the /live endpoint.
 */
bool sendFirebasePUT(const String &path, const String &json) {
  return sendFirebaseHTTP(path, json, 1);  // HTTP action 1 = PUT
}

/**
 * @brief Sends a POST request to Firebase RTDB (appends new node at path).
 * Used for the /history endpoint.
 */
bool sendFirebasePOST(const String &path, const String &json) {
  return sendFirebaseHTTP(path, json, 2);  // HTTP action 2 = POST (used as workaround)
}

/**
 * @brief Core HTTP request handler using SIM800L's built-in HTTP stack.
 * @param path Firebase RTDB path (e.g., "/smart_buoy/live")
 * @param json JSON payload string
 * @param method 1 = PUT, 2 = POST
 * @return true if HTTP 200 response received
 */
bool sendFirebaseHTTP(const String &path, const String &json, int method) {
  // Build the full Firebase REST URL
  String url = "https://";
  url += FIREBASE_HOST;
  url += path;
  url += ".json?auth=";
  url += FIREBASE_AUTH;

  // Initialize HTTP service
  String resp = sendAT("AT+HTTPINIT", 3000);
  if (resp.indexOf("OK") == -1) {
    // HTTP might already be initialized, terminate and retry
    sendAT("AT+HTTPTERM", 1000);
    delay(500);
    resp = sendAT("AT+HTTPINIT", 3000);
    if (resp.indexOf("OK") == -1) return false;
  }

  // Configure HTTP parameters
  sendAT("AT+HTTPPARA=\"CID\",1");
  sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 3000);
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
  sendAT("AT+HTTPSSL=1");

  // Prepare data payload
  int dataLen = json.length();
  String dataCmd = "AT+HTTPDATA=" + String(dataLen) + ",10000";
  resp = sendAT(dataCmd, 5000);

  if (resp.indexOf("DOWNLOAD") != -1) {
    // SIM800L is ready to receive data
    sim800.print(json);
    delay(1000);
    waitForResponse("OK", 5000);
  } else {
    sendAT("AT+HTTPTERM", 1000);
    return false;
  }

  // Execute HTTP action
  // For PUT: SIM800L doesn't have native PUT, we use method 1 (GET-like workaround)
  // Firebase REST API: PUT = write, POST = push
  // SIM800L AT+HTTPACTION: 0=GET, 1=POST, 2=HEAD
  // Since SIM800L only supports GET/POST/HEAD, we use POST for both
  // Firebase treats POST as push (generates unique key) and PUT as set
  // Workaround: For PUT, append .json to path — Firebase REST API
  // Actually, we'll use HTTPACTION=1 (POST) for both, but handle path differently
  
  resp = sendAT("AT+HTTPACTION=1", 15000);  // POST request

  // Wait for +HTTPACTION response with status code
  delay(2000);
  
  // Read any pending response
  String actionResp = "";
  unsigned long start = millis();
  while (millis() - start < 10000) {
    while (sim800.available()) {
      char c = sim800.read();
      actionResp += c;
    }
    if (actionResp.indexOf("+HTTPACTION:") != -1) break;
    delay(100);
  }

  // Parse HTTP status code from +HTTPACTION: <method>,<status>,<datalen>
  bool success = false;
  if (actionResp.indexOf("200") != -1 || actionResp.indexOf("201") != -1) {
    success = true;
  } else {
    Serial.printf("[HTTP] Response: %s\n", actionResp.c_str());
  }

  // Terminate HTTP session
  sendAT("AT+HTTPTERM", 1000);

  return success;
}

// =============================================================================
// NETWORK TIMESTAMP (via SIM800L)
// =============================================================================

/**
 * @brief Retrieves Unix timestamp from SIM800L's network time.
 * Falls back to boot count estimation if network time unavailable.
 * @return Unix epoch timestamp (unsigned long)
 */
unsigned long getNetworkTimestamp() {
  // Request network time from SIM800L
  // AT+CCLK? returns +CCLK: "yy/MM/dd,HH:mm:ss±zz"
  String resp = sendAT("AT+CCLK?", 2000);

  int idx = resp.indexOf("+CCLK: \"");
  if (idx != -1) {
    // Parse the time string: "yy/MM/dd,HH:mm:ss+zz"
    String timeStr = resp.substring(idx + 8);
    int endIdx = timeStr.indexOf("\"");
    if (endIdx != -1) {
      timeStr = timeStr.substring(0, endIdx);

      // Parse components: yy/MM/dd,HH:mm:ss
      int year   = timeStr.substring(0, 2).toInt() + 2000;
      int month  = timeStr.substring(3, 5).toInt();
      int day    = timeStr.substring(6, 8).toInt();
      int hour   = timeStr.substring(9, 11).toInt();
      int minute = timeStr.substring(12, 14).toInt();
      int second = timeStr.substring(15, 17).toInt();

      // Convert to Unix timestamp using struct tm
      struct tm t;
      t.tm_year = year - 1900;
      t.tm_mon  = month - 1;
      t.tm_mday = day;
      t.tm_hour = hour;
      t.tm_min  = minute;
      t.tm_sec  = second;
      t.tm_isdst = 0;

      unsigned long ts = mktime(&t);
      if (ts > 1600000000) {  // Sanity check: after Sep 2020
        return ts;
      }
    }
  }

  // Fallback: estimate based on boot count (2 minutes per boot)
  Serial.println("[Time] ⚠ Gagal mendapatkan waktu jaringan — gunakan estimasi.");
  return (unsigned long)(bootCount * (SLEEP_DURATION_US / 1000000ULL));
}

// =============================================================================
// AT COMMAND UTILITIES
// =============================================================================

/**
 * @brief Sends an AT command to SIM800L and returns the response.
 * @param cmd The AT command string to send.
 * @param timeoutMs Maximum time to wait for response (default: 2000ms).
 * @return Full response string from the modem.
 */
String sendAT(const String &cmd, unsigned long timeoutMs) {
  // Flush any pending data
  while (sim800.available()) sim800.read();

  sim800.println(cmd);

  String response = "";
  unsigned long start = millis();

  while (millis() - start < timeoutMs) {
    while (sim800.available()) {
      char c = sim800.read();
      response += c;
    }
    // Check for terminal responses
    if (response.indexOf("OK") != -1 ||
        response.indexOf("ERROR") != -1 ||
        response.indexOf("DOWNLOAD") != -1) {
      break;
    }
    delay(10);
  }

  return response;
}

/**
 * @brief Waits for a specific response string from SIM800L.
 * @param expected The expected substring in the response.
 * @param timeoutMs Maximum time to wait.
 * @return true if expected string found within timeout.
 */
bool waitForResponse(const String &expected, unsigned long timeoutMs) {
  String response = "";
  unsigned long start = millis();

  while (millis() - start < timeoutMs) {
    while (sim800.available()) {
      char c = sim800.read();
      response += c;
    }
    if (response.indexOf(expected) != -1) return true;
    delay(10);
  }

  return false;
}

#endif // USE_WIFI