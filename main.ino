#include <WiFi.h>
#include <FirebaseESP32.h> // Instal Library: "Firebase ESP32 Client" by Mobizt di Arduino IDE
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define SDA_PIN 27
#define SCL_PIN 26
#define PH_PIN 34
#define TEMP_PIN 4
#define TURB_PIN 35

// Konfigurasi WiFi
const char* ssid = "No Internet Connection";
const char* password = "Loupatty143";

// Konfigurasi Firebase (Ganti dengan milik Anda)
#define FIREBASE_HOST "URL_DATABASE_FIREBASE_ANDA.firebaseio.com" // Tanpa https:// dan simbol (/) di akhir
#define FIREBASE_AUTH "DATABASE_SECRET_FIREBASE_ANDA"             // Ini adalah token rahasia dari setting database

FirebaseData firebaseData;

LiquidCrystal_I2C lcd(0x27, 16, 2);

// DS18B20
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

// kalibrasi pH (Tetap tidak diubah)
float m = -4.79;
float b = 18.06;

// ===== fungsi pH =====
float readVoltage() {
  int total = 0;
  for (int i = 0; i < 20; i++) {
    total += analogRead(PH_PIN);
    delay(10);
  }
  float adc = total / 20.0;
  return adc * (3.3 / 4095.0);
}

// ===== turbidity =====
int readTurbidity() {
  int total = 0;
  for (int i = 0; i < 10; i++) {
    total += analogRead(TURB_PIN);
    delay(5);
  }
  return total / 10;
}

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();

  analogSetAttenuation(ADC_11db);
  sensors.begin();

  lcd.setCursor(0,0);
  lcd.print("Connecting WiFi");
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected!");
  Serial.println(WiFi.localIP());

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("WiFi Connected!!");
  delay(1000);

  // Inisialisasi Firebase
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Connecting to");
  lcd.setCursor(0,1);
  lcd.print("Firebase...");
  
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  Firebase.reconnectWiFi(true); // Sangat penting agar Firebase menyambung ulang otomatis kalau WiFi putus!
  
  delay(1000);
  lcd.clear();
}

unsigned long previousMillis = 0;
const long updateInterval = 2000;

void loop() {
  unsigned long currentMillis = millis();

  // Eksekusi setiap 2 detik (Baca sensor, Update LCD, & Kirim ke Firebase)
  if (currentMillis - previousMillis >= updateInterval) {
    previousMillis = currentMillis;

    // 1. Baca Sensor (Sensors section)
    float voltage = readVoltage();
    float phValue = (m * voltage) + b;

    sensors.requestTemperatures();
    float tempC = sensors.getTempCByIndex(0);

    int turbidity = readTurbidity();
    String kondisi;
    if (turbidity < 1500) kondisi = "Jernih";
    else if (turbidity < 3000) kondisi = "Keruh";
    else kondisi = "Kotor";

    // 2. Tampil di LCD secara mulus (tanpa berkedip)
    lcd.setCursor(0,0);
    lcd.print("pH: ");
    lcd.print(phValue, 2);
    lcd.print("      ");

    lcd.setCursor(0,1);
    lcd.print("T: ");
    lcd.print(tempC, 1);
    lcd.print((char)223);
    lcd.print("C      ");

    // 3. Kirim dan Simpan Data ke Firebase Realtime Database
    // Data ini akan dikelompokkan ke dalam direktori "/smart_buoy/"
    Firebase.setFloat(firebaseData, "/smart_buoy/pH", phValue);
    Firebase.setFloat(firebaseData, "/smart_buoy/suhu", tempC);
    Firebase.setInt(firebaseData, "/smart_buoy/kekeruhan", turbidity);
    Firebase.setString(firebaseData, "/smart_buoy/status_kekeruhan", kondisi);

    // Optional: Print logika ke Serial Monitor jika sedang colok laptop
    Serial.print("pH: "); Serial.print(phValue);
    Serial.print(" | Suhu: "); Serial.print(tempC);
    Serial.print(" | Kekeruhan: "); Serial.println(kondisi);
  }
}