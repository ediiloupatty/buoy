#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define SDA_PIN 27
#define SCL_PIN 26
#define PH_PIN 34
#define TEMP_PIN 4
#define TURB_PIN 35

// WIFI
const char* ssid = "No Internet Connection";
const char* password = "Loupatty143"; // ganti kalau beda

WebServer server(80);

LiquidCrystal_I2C lcd(0x27, 16, 2);

// DS18B20
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

// kalibrasi pH
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

// ===== halaman web =====
void handleRoot() {
  float voltage = readVoltage();
  float phValue = (m * voltage) + b;

  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);

  int turbidity = readTurbidity();

  String kondisi;
  if (turbidity < 1500) kondisi = "Jernih";
  else if (turbidity < 3000) kondisi = "Keruh";
  else kondisi = "Kotor";

  String html = "<html><head><meta http-equiv='refresh' content='2'></head><body>";
  html += "<h1>Water Monitoring</h1>";
  html += "<p>pH: " + String(phValue, 2) + "</p>";
  html += "<p>Temperature: " + String(tempC, 2) + " C</p>";
  html += "<p>Turbidity: " + String(turbidity) + " (" + kondisi + ")</p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
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
  lcd.print("WiFi Connected");
  lcd.setCursor(0,1);
  lcd.print(WiFi.localIP());

  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  server.handleClient();

  // tetap tampil di LCD juga
  float voltage = readVoltage();
  float phValue = (m * voltage) + b;

  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);

  int turbidity = readTurbidity();

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("pH:");
  lcd.print(phValue,2);

  lcd.setCursor(0,1);
  lcd.print("T:");
  lcd.print(tempC,1);
  lcd.print((char)223);
  lcd.print("C");

  delay(2000);
}