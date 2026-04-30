#include <WiFi.h>
#include <FirebaseESP32.h>

#include <WebServer.h>   // Library tambahan untuk Web Server
#include <time.h>        // Library NTP untuk sinkronisasi waktu

// Mengimpor file konfigurasi dan file modul sensor buatan kita sendiri
#include "Config.h"
#include "Sensors.h"

// Objek Web Server di port 80
WebServer server(80);

FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

// Variabel global untuk menyimpan data sensor agar bisa ditampilkan di web
float phValue = 0;
float tempC = 0;
int turbidity = 0;
String kondisi = "";
unsigned long lastMillis = 0;
unsigned long lastHistoryMillis = 0;

// Variabel untuk Threshold pengiriman Live
float lastSentPH = -100.0;
float lastSentTemp = -100.0;
int lastSentTurb = -100;

// Interval Konfigurasi
const unsigned long SENSOR_INTERVAL   = 10000;   // 10 detik — baca sensor & update lokal + Firebase live
const unsigned long HISTORY_INTERVAL  = 600000;  // 10 menit — simpan log historis ke Supabase

// NTP Konfigurasi (Zona Waktu Indonesia Timur / WIT = UTC+9)
// Sesuaikan offset jika lokasi berbeda:
//   WIB  = 7*3600  (UTC+7)
//   WITA = 8*3600  (UTC+8)
//   WIT  = 9*3600  (UTC+9)
const long GMT_OFFSET_SEC    = 9 * 3600; // WIT (Maluku/Papua)
const int  DAYLIGHT_OFFSET   = 0;        // Indonesia tidak pakai DST
const char *NTP_SERVER       = "pool.ntp.org";

/**
 * handleRoot: Fungsi untuk menyajikan tampilan Dashboard saat IP ESP32 diakses.
 */
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<meta http-equiv='refresh' content='10'>"; // Auto refresh tiap 10 detik (sinkron dengan pembacaan sensor)
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
  // Card Suhu
  html += "  <div class='card'><h3>Suhu Air</h3><div class='val'>" + String(tempC, 1) + "<span class='unit'>&deg;C</span></div></div>";
  // Card pH
  html += "  <div class='card'><h3>Nilai pH</h3><div class='val' style='color:#27ae60;'>" + String(phValue, 2) + "</div></div>";
  // Card Kekeruhan
  html += "  <div class='card'><h3>Kekeruhan</h3><div class='val' style='color:#f39c12;'>" + String(turbidity) + "</div><div class='status-box'>" + kondisi + "</div></div>";
  html += "</div>";
  
  // Tampilkan waktu real dari NTP, bukan hanya detik sejak boot
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

void setup() {
  Serial.begin(115200);

  // 1. Inisialisasi Sensor
  initSensors();

  // 2. Inisialisasi WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi Connected!");
  Serial.print("Akses Monitoring di IP: ");
  Serial.println(WiFi.localIP());

  // 3. Inisialisasi Firebase
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  
  // 4. Sinkronisasi waktu via NTP (wajib setelah WiFi connected)
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

  // 5. Inisialisasi rute Web Server
  server.on("/", handleRoot);
  server.begin();
  Serial.println("Web Server Started!");
}

void loop() {
  // Menangani permintaan dari browser
  server.handleClient();

  unsigned long now = millis();

  // ── Timer 1: Setiap 10 DETIK — Baca sensor + update Firebase /live/ ──
  if (now - lastMillis >= SENSOR_INTERVAL) {
    lastMillis = now;

    // Ambil Data Sensor
    phValue   = readPH();
    tempC     = readTemperature();
    turbidity = readTurbidityValue();
    kondisi   = getTurbidityStatus(turbidity);

    Serial.printf("[Sensor] Suhu=%.1f°C | pH=%.2f | Turb=%d (%s)\n",
                  tempC, phValue, turbidity, kondisi.c_str());

    // Update data real-time di Firebase (path /live/ — ditimpa terus)
    // Menggunakan 1 request JSON batch (hemat ~73% bandwidth vs 4 request terpisah)
    // Field status_kekeruhan dihapus — dihitung di sisi Flutter app dari nilai kekeruhan
    // Terapkan "Threshold" (Kirim Kalau Berubah Saja) untuk Data Live
    bool shouldSendLive = false;
    if (abs(phValue - lastSentPH) > 0.05 || abs(tempC - lastSentTemp) > 0.1 || abs(turbidity - lastSentTurb) > 5) {
      shouldSendLive = true;
    }

    if (shouldSendLive && Firebase.ready()) {
      FirebaseJson liveJson;
      // Pendekkan Nama Kunci JSON
      liveJson.set("pH", phValue);
      liveJson.set("temp", tempC);
      liveJson.set("turb", turbidity);
      if (Firebase.setJSON(firebaseData, "/smart_buoy/live", liveJson)) {
        lastSentPH = phValue;
        lastSentTemp = tempC;
        lastSentTurb = turbidity;
      }
    }
  }

  // ── Timer 2: Setiap 10 MENIT — Simpan log historis ke Firebase RTDB ──
  // Data ini digunakan untuk log dan aplikasi mobile
  if (now - lastHistoryMillis >= HISTORY_INTERVAL) {
    lastHistoryMillis = now;
    sendHistoryToFirebase();
  }
}

/**
 * sendHistoryToFirebase: Mengirim data sensor historis ke Firebase RTDB.
 * Path target: /smart_buoy/history
 * Data akan ditambahkan (push) bukan ditimpa, sehingga membentuk list.
 */
void sendHistoryToFirebase() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Firebase History] WiFi tidak terhubung — skip.");
    return;
  }

  // Ambil waktu dari NTP
  struct tm timeinfo;
  unsigned long timestamp = 0;

  if (getLocalTime(&timeinfo)) {
    timestamp = mktime(&timeinfo); // Mendapatkan unix epoch time
  }

  if (Firebase.ready() && timestamp > 0) {
    FirebaseJson historyJson;
    // Hapus Data String Redundan & Pendekkan Nama Kunci
    historyJson.set("temp", tempC);
    historyJson.set("pH", phValue);
    historyJson.set("turb", turbidity);
    historyJson.set("ts", timestamp);

    // Gunakan pushJSON agar data ditambahkan (membuat ID unik)
    if (Firebase.pushJSON(firebaseData, "/smart_buoy/history", historyJson)) {
      Serial.printf("[Firebase History] ✓ Data tersimpan (ts: %lu)\n", timestamp);
    } else {
      Serial.printf("[Firebase History] ✗ Gagal: %s\n", firebaseData.errorReason().c_str());
    }
  } else {
    Serial.println("[Firebase History] ✗ Firebase belum ready atau waktu belum sinkron.");
  }
}