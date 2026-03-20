#include <WiFi.h>
#include <FirebaseESP32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Mengimpor file konfigurasi dan file modul sensor buatan kita sendiri
#include "Config.h"
#include "Sensors.h"

FirebaseData firebaseData;
LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  Serial.begin(115200);

  // 1. Inisialisasi Layar LCD
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Memulai Sistem..");

  // 2. Inisialisasi Kumpulan Sensor 
  initSensors();

  // 3. Inisialisasi WiFi
  WiFi.begin(ssid, password);
  lcd.clear();
  lcd.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  // Koneksi Berhasil
  Serial.println("\nConnected!");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("WiFi Connected!!");
  delay(1000);

  // 4. Inisialisasi Firebase
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Connecting to");
  lcd.setCursor(0,1);
  lcd.print("Firebase...");
  
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  Firebase.reconnectWiFi(true); // Sangat penting agar Firebase menyambung ulang otomatis kalau WiFi putus
  
  delay(1000);
  lcd.clear();
}

unsigned long previousMillis = 0;
const long updateInterval = 2000;

void loop() {
  unsigned long currentMillis = millis();

  // Eksekusi pembaruan data sensor dan UI LCD cukup setiap 2 detik
  if (currentMillis - previousMillis >= updateInterval) {
    previousMillis = currentMillis;

    // A. Mengambil Data Bacaan (Memanggil fungsi dari Sensors.cpp)
    float phValue   = readPH();
    float tempC     = readTemperature();
    int turbidity   = readTurbidityValue();
    String kondisi  = getTurbidityStatus(turbidity);

    // B. Menampilkannya ke LCD secara mulus (tanpa berkedip/lcd.clear)
    lcd.setCursor(0,0);
    lcd.print("pH: ");
    lcd.print(phValue, 2);
    lcd.print("      "); // spasi untuk membersihkan karakter lama

    lcd.setCursor(0,1);
    lcd.print("T: ");
    lcd.print(tempC, 1);
    lcd.print((char)223);
    lcd.print("C      "); // spasi untuk membersihkan karakter lama

    // C. Mengirim dan Menyimpan Data ke Firebase Realtime Database
    Firebase.setFloat(firebaseData, "/smart_buoy/pH", phValue);
    Firebase.setFloat(firebaseData, "/smart_buoy/suhu", tempC);
    Firebase.setInt(firebaseData, "/smart_buoy/kekeruhan", turbidity);
    Firebase.setString(firebaseData, "/smart_buoy/status_kekeruhan", kondisi);

    // (Opsional) Print Logika ke C-Serial Monitor untuk Debugging USB
    Serial.print("pH: "); Serial.print(phValue);
    Serial.print(" | Suhu: "); Serial.print(tempC);
    Serial.print(" | Kekeruhan: "); Serial.println(kondisi);
  }
}