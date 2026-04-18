#include <WiFi.h>
#include <FirebaseESP32.h>
#include <WebServer.h> // Library tambahan untuk Web Server

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

/**
 * handleRoot: Fungsi untuk menyajikan tampilan Dashboard saat IP ESP32 diakses.
 */
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<meta http-equiv='refresh' content='5'>"; // Auto refresh tiap 5 detik
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
  
  html += "<p style='color: #7f8c8d; margin-top: 30px;'>Last Update: " + String(millis()/1000) + " seconds since boot</p>";
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
  
  // 4. Inisialisasi rute Web Server
  server.on("/", handleRoot);
  server.begin();
  Serial.println("Web Server Started!");
}

void loop() {
  // Menangani permintaan dari browser
  server.handleClient();

  // Membaca sensor dan mengirim data setiap 5 detik
  if (millis() - lastMillis > 5000) {
    lastMillis = millis();

    // Ambil Data Sensor
    phValue   = readPH();
    tempC     = readTemperature();
    turbidity = readTurbidityValue();
    kondisi  = getTurbidityStatus(turbidity);

    // Kirim ke Firebase (Opsional: Tetap dikirim agar data tersimpan di Cloud)
    if (Firebase.ready()) {
      Firebase.setFloat(firebaseData, "/smart_buoy/pH", phValue);
      Firebase.setFloat(firebaseData, "/smart_buoy/suhu", tempC);
      Firebase.setInt(firebaseData, "/smart_buoy/kekeruhan", turbidity);
      Firebase.setString(firebaseData, "/smart_buoy/status_kekeruhan", kondisi);
      Serial.println("Data Berhasil di-Update ke Cloud & Local.");
    }
  }
}