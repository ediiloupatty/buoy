/*
 * Diagnostik LoRa Ra-02 (SX1278) — baca REG_VERSION langsung via SPI.
 * Upload ke ESP32 yang tersambung ke modul LoRa, lihat Serial 115200.
 */
#include <SPI.h>

#define PIN_SCK  18
#define PIN_MISO 19
#define PIN_MOSI 23
#define PIN_SS    5
#define PIN_RST  14   // dipakai hanya untuk test "dengan reset"

byte readReg(byte addr) {
  byte v;
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_SS, LOW);
  SPI.transfer(addr & 0x7F);   // bit7=0 → read
  v = SPI.transfer(0x00);
  digitalWrite(PIN_SS, HIGH);
  SPI.endTransaction();
  return v;
}

void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println("\n================ LoRa SPI DIAGNOSTIC ================");

  pinMode(PIN_SS, OUTPUT);
  digitalWrite(PIN_SS, HIGH);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);

  // Test A: kondisi 6 kabel sekarang (RST TIDAK dikabel ke ESP)
  byte vA = readReg(0x42);
  Serial.printf("REG_VERSION (mode 6-kabel, tanpa RST)   = 0x%02X\n", vA);

  // Test B: paksa reset lewat GPIO14 (seolah RST dikabel)
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);  delay(10);
  digitalWrite(PIN_RST, HIGH); delay(10);
  byte vB = readReg(0x42);
  Serial.printf("REG_VERSION (setelah reset via GPIO14)   = 0x%02X\n", vB);

  Serial.println("----------------------------------------------------");
  Serial.println("Interpretasi nilai:");
  Serial.println("  0x12 = MODUL OK terdeteksi  ✓");
  Serial.println("  0x00 = MISO mati / modul ditahan reset / tidak ada power");
  Serial.println("  0xFF = MISO ngambang (kabel MISO tidak nyambung)");
  Serial.println("====================================================");

  if (vA != 0x12 && vB == 0x12)
    Serial.println(">> KESIMPULAN: Modul butuh RST! Sambungkan RST -> GPIO14.");
  else if (vA == 0x12 || vB == 0x12)
    Serial.println(">> KESIMPULAN: Modul TERDETEKSI. Masalah ada di kode/library.");
  else if (vA == 0xFF && vB == 0xFF)
    Serial.println(">> KESIMPULAN: MISO (GPIO19) kemungkinan tidak nyambung.");
  else
    Serial.println(">> KESIMPULAN: Cek POWER(3V3)/GND/MOSI/SCK/NSS — modul tdk merespons.");

  // ── Tes objektif jalur MISO (GPIO19) ──
  SPI.end();
  pinMode(PIN_MISO, INPUT_PULLUP);   delay(5); int up   = digitalRead(PIN_MISO);
  pinMode(PIN_MISO, INPUT_PULLDOWN); delay(5); int down = digitalRead(PIN_MISO);
  Serial.println("\n--- TES JALUR MISO (GPIO19) ---");
  Serial.printf("  pull-up baca=%d | pull-down baca=%d\n", up, down);
  if (up == 1 && down == 0)
    Serial.println("  >> MISO NGAMBANG: kabel MISO putus / modul TIDAK dapat power.");
  else if (up == 1 && down == 1)
    Serial.println("  >> MISO ketarik HIGH: ada perangkat di jalur (cek SCK/MOSI/NSS/power).");
  else if (up == 0 && down == 0)
    Serial.println("  >> MISO ketarik LOW: korslet ke GND / pin salah.");
}

void loop() { delay(2000); }
