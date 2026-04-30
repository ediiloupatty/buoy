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
 * Handles network connectivity, NTP time synchronization, a local diagnostic 
 * web server, and dual-pipeline Firebase Realtime Database telemetry streams.
 */

#include <WiFi.h>
#include <FirebaseESP32.h>
#include <WebServer.h>   // Diagnostic Local Web Server
#include <time.h>        // Network Time Protocol (NTP) Client

// Custom Hardware Modules
#include "Config.h"
#include "Sensors.h"

// Instantiate HTTP server on port 80
WebServer server(80);

// Firebase Core Objects
FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

// Global Telemetry State
float phValue = 0;
float tempC = 0;
int turbidity = 0;
String kondisi = "";

// Scheduling Timers
unsigned long lastMillis = 0;
unsigned long lastHistoryMillis = 0;

// Threshold states for bandwidth optimization
float lastSentPH = -100.0;
float lastSentTemp = -100.0;
int lastSentTurb = -100;

// Operational Intervals
const unsigned long SENSOR_INTERVAL   = 10000;   ///< 10 Seconds: Poll sensors & push Live data
const unsigned long HISTORY_INTERVAL  = 600000;  ///< 10 Minutes: Push to historical Time-Series Data Lake

// NTP Timezone Configuration (WIT: Eastern Indonesia Time = UTC+9)
const long GMT_OFFSET_SEC    = 9 * 3600; 
const int  DAYLIGHT_OFFSET   = 0;        
const char *NTP_SERVER       = "pool.ntp.org";

/**
 * @brief HTTP GET handler for the local diagnostic dashboard.
 * Serves an auto-refreshing HTML interface containing current telemetry.
 */
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<meta http-equiv='refresh' content='10'>"; // Auto-refresh synchronized with SENSOR_INTERVAL
  html += "<title>Smart Buoy Local Monitoring</title>";
  html += "<style>";
  html += "  body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; text-align: center; background-color: #f4f7f6; color: #333; padding: 20px; }";
  html += "  .container { display: flex; flex-wrap: wrap; justify-content: center; gap: 20px; }";
  html += "  .card { background: white; border-radius: 12px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); padding: 20px; width: 250px; }";
  html += "  h1 { color: #2c3e50; }";
  html += "  .val { font-size: 2.5em; font-weight: bold; color: #3498db; }";
  html += "  .unit { font-size: 0.5em; }";
  html += "  .status-box { margin-top: 10px; font-weight: bold; color: #e67e22; }";
  html += "</style></head><body>";
  
  html += "<h1>Smart Buoy Local Monitor</h1>";
  html += "<p>Pemantauan Kualitas Air Langsung dari Alat</p>";
  
  html += "<div class='container'>";
  // Temperature Card
  html += "  <div class='card'><h3>Suhu Air</h3><div class='val'>" + String(tempC, 1) + "<span class='unit'>&deg;C</span></div></div>";
  // pH Card
  html += "  <div class='card'><h3>Nilai pH</h3><div class='val' style='color:#27ae60;'>" + String(phValue, 2) + "</div></div>";
  // Turbidity Card
  html += "  <div class='card'><h3>Kekeruhan</h3><div class='val' style='color:#f39c12;'>" + String(turbidity) + "</div><div class='status-box'>" + kondisi + "</div></div>";
  html += "</div>";
  
  // Format current NTP time for the dashboard
  struct tm timeinfo;
  String timeStr = "Belum tersinkronisasi";
  if (getLocalTime(&timeinfo)) {
    char buf[30];
    strftime(buf, sizeof(buf), "%d %b %Y  %H:%M:%S", &timeinfo);
    timeStr = String(buf);
  }
  html += "<p style='color: #7f8c8d; margin-top: 30px;'>Terakhir: " + timeStr + "</p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

/**
 * @brief System Entry Point. Initializes hardware, networking, and backend services.
 */
void setup() {
  Serial.begin(115200);

  // 1. Initialize Hardware Abstraction Layer
  initSensors();

  // 2. Establish Network Connection
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi Connected!");
  Serial.print("Akses Monitoring di IP: ");
  Serial.println(WiFi.localIP());

  // 3. Initialize Firebase RTDB Client
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  
  // 4. Synchronize Internal RTC via Network Time Protocol
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET, NTP_SERVER);
  Serial.print("Sinkronisasi waktu NTP");
  struct tm timeinfo;
  int retries = 0;
  while (!getLocalTime(&timeinfo) && retries < 10) {
    Serial.print(".");
    delay(500);
    retries++;
  }
  if (retries < 10) {
    char buf[30];
    strftime(buf, sizeof(buf), "%d %b %Y %H:%M:%S", &timeinfo);
    Serial.printf("\nWaktu: %s\n", buf);
  } else {
    Serial.println("\n[NTP] Gagal sinkronisasi — history path akan fallback ke millis.");
  }

  // 5. Boot Local Web Server
  server.on("/", handleRoot);
  server.begin();
  Serial.println("Web Server Started!");
}

/**
 * @brief Main execution loop handling HTTP requests and asynchronous telemetry tasks.
 */
void loop() {
  // Process incoming local HTTP requests
  server.handleClient();

  unsigned long now = millis();

  // ── TASK 1: Live Telemetry Stream (High Frequency) ──
  if (now - lastMillis >= SENSOR_INTERVAL) {
    lastMillis = now;

    // Poll Hardware
    phValue   = readPH();
    tempC     = readTemperature();
    turbidity = readTurbidityValue();
    kondisi   = getTurbidityStatus(turbidity);

    Serial.printf("[Sensor] Suhu=%.1f°C | pH=%.2f | Turb=%d (%s)\n",
                  tempC, phValue, turbidity, kondisi.c_str());

    // Bandwidth Optimization: Only dispatch payload if delta exceeds predefined thresholds
    bool shouldSendLive = false;
    if (abs(phValue - lastSentPH) > 0.05 || abs(tempC - lastSentTemp) > 0.1 || abs(turbidity - lastSentTurb) > 5) {
      shouldSendLive = true;
    }

    if (shouldSendLive && Firebase.ready()) {
      FirebaseJson liveJson;
      // Utilize shortened keys to minimize JSON payload weight
      liveJson.set("pH", phValue);
      liveJson.set("temp", tempC);
      liveJson.set("turb", turbidity);
      
      // Overwrite the live node for real-time dashboarding
      if (Firebase.setJSON(firebaseData, "/smart_buoy/live", liveJson)) {
        lastSentPH = phValue;
        lastSentTemp = tempC;
        lastSentTurb = turbidity;
      }
    }
  }

  // ── TASK 2: Historical Telemetry Stream (Low Frequency) ──
  if (now - lastHistoryMillis >= HISTORY_INTERVAL) {
    lastHistoryMillis = now;
    sendHistoryToFirebase();
  }
}

/**
 * @brief Dispatches a time-stamped telemetry payload to the historical Data Lake.
 * Appends data as a new unique node rather than overwriting.
 */
void sendHistoryToFirebase() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Firebase History] WiFi tidak terhubung — skip.");
    return;
  }

  // Retrieve Unix Epoch from synchronized RTC
  struct tm timeinfo;
  unsigned long timestamp = 0;

  if (getLocalTime(&timeinfo)) {
    timestamp = mktime(&timeinfo);
  }

  if (Firebase.ready() && timestamp > 0) {
    FirebaseJson historyJson;
    // Utilize shortened keys; string states are computed client-side
    historyJson.set("temp", tempC);
    historyJson.set("pH", phValue);
    historyJson.set("turb", turbidity);
    historyJson.set("ts", timestamp);

    // Push JSON to generate a unique chronological key
    if (Firebase.pushJSON(firebaseData, "/smart_buoy/history", historyJson)) {
      Serial.printf("[Firebase History] ✓ Data tersimpan (ts: %lu)\n", timestamp);
    } else {
      Serial.printf("[Firebase History] ✗ Gagal: %s\n", firebaseData.errorReason().c_str());
    }
  } else {
    Serial.println("[Firebase History] ✗ Firebase belum ready atau waktu belum sinkron.");
  }
}