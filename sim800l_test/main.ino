/**
 * @file main.ino
 * @brief SIM800L Diagnostic Sketch — ESP32
 *
 * Tujuan: Verifikasi komunikasi antara ESP32 dan SIM800L V2
 *
 * Wiring:
 *   SIM800L TX  →  ESP32 GPIO 16
 *   SIM800L RX  ←  ESP32 GPIO 17
 *   SIM800L RST ←  ESP32 GPIO 5
 *   SIM800L VCC ←  5V (dari XL6009 boost converter)
 *   SIM800L GND →  GND ESP32 (satukan semua GND!)
 *
 * Cara pakai:
 *   1. Upload sketch ini ke ESP32
 *   2. Buka Serial Monitor, baud rate: 115200
 *   3. Lihat output — ikuti petunjuk yang muncul
 */

#include <HardwareSerial.h>

// ── Pin Definitions ──────────────────────────────────────────────────────────
#define SIM_RX_PIN  16    // SIM800L TX  → ESP32 GPIO 16
#define SIM_TX_PIN  17    // SIM800L RX  ← ESP32 GPIO 17
#define SIM_RST_PIN  5    // SIM800L RST ← ESP32 GPIO 5

// Baud rates to try (SIM800L bisa auto-baud, coba dari yang paling umum)
const long BAUD_RATES[] = {9600, 115200, 57600, 38400, 19200, 4800};
const int  BAUD_COUNT   = 6;

HardwareSerial sim800(2);  // UART2

// ── Function Declarations ─────────────────────────────────────────────────────
String  sendAT(const String &cmd, unsigned long timeoutMs = 2000);
bool    tryBaudRate(long baud);
void    resetModem();
void    runDiagnostic();
void    printSeparator(const String &title);

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  printSeparator("SIM800L DIAGNOSTIC v1.0");
  Serial.println("Wiring yang digunakan:");
  Serial.printf("  SIM800L TX  --> ESP32 GPIO %d\n", SIM_RX_PIN);
  Serial.printf("  SIM800L RX  <-- ESP32 GPIO %d\n", SIM_TX_PIN);
  Serial.printf("  SIM800L RST <-- ESP32 GPIO %d\n", SIM_RST_PIN);
  Serial.println("  SIM800L VCC <-- 5V (XL6009)");
  Serial.println("  SIM800L GND --> GND ESP32");
  printSeparator("");

  delay(1000);
  runDiagnostic();
}

// =============================================================================
// LOOP — Bisa kirim AT command manual via Serial Monitor
// =============================================================================
void loop() {
  // Forward Serial Monitor input ke SIM800L
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      Serial.printf("\n>>> Kirim: %s\n", cmd.c_str());
      String resp = sendAT(cmd, 3000);
      Serial.printf("<<< Terima: %s\n", resp.c_str());
    }
  }

  // Forward SIM800L output ke Serial Monitor
  while (sim800.available()) {
    Serial.write(sim800.read());
  }
}

// =============================================================================
// DIAGNOSTIC MAIN FLOW
// =============================================================================
void runDiagnostic() {

  // ── STEP 1: Coba tiap baud rate ────────────────────────────────────────────
  printSeparator("STEP 1: Auto-detect Baud Rate");
  
  long detectedBaud = 0;
  for (int i = 0; i < BAUD_COUNT; i++) {
    Serial.printf("[Test] Mencoba %ld bps... ", BAUD_RATES[i]);
    if (tryBaudRate(BAUD_RATES[i])) {
      detectedBaud = BAUD_RATES[i];
      Serial.printf("✓ BERHASIL! SIM800L merespons di %ld bps\n", detectedBaud);
      break;
    } else {
      Serial.println("✗ Tidak ada respons.");
    }
  }

  if (detectedBaud == 0) {
    // ── STEP 1b: Coba RST reset dulu, lalu scan ulang ────────────────────────
    printSeparator("STEP 1b: RST Reset lalu coba lagi");
    resetModem();
    
    for (int i = 0; i < BAUD_COUNT; i++) {
      Serial.printf("[Test] Mencoba %ld bps... ", BAUD_RATES[i]);
      if (tryBaudRate(BAUD_RATES[i])) {
        detectedBaud = BAUD_RATES[i];
        Serial.printf("✓ BERHASIL setelah RST! Baud: %ld bps\n", detectedBaud);
        break;
      } else {
        Serial.println("✗ Tidak ada respons.");
      }
    }
  }

  if (detectedBaud == 0) {
    printSeparator("❌ GAGAL — SIM800L Tidak Terdeteksi");
    Serial.println("Kemungkinan penyebab:");
    Serial.println("  1. Kabel TX/RX masih terbalik — coba tukar lagi");
    Serial.println("  2. GND SIM800L belum disambung ke GND ESP32");
    Serial.println("  3. Power 5V tidak stabil atau belum terhubung");
    Serial.println("  4. Modul SIM800L rusak");
    Serial.println("");
    Serial.println("Mode manual aktif — ketik AT command di Serial Monitor:");
    return;
  }

  // ── STEP 2: Konfigurasi ke 9600 (standar) jika beda ─────────────────────── 
  if (detectedBaud != 9600) {
    printSeparator("STEP 2: Set Baud Rate ke 9600");
    Serial.printf("Modem terdeteksi di %ld bps, set ke 9600...\n", detectedBaud);
    // Minta modem ganti baud rate ke 9600
    sendAT("AT+IPR=9600");
    delay(500);
    sim800.end();
    sim800.begin(9600, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
    delay(500);
    String test = sendAT("AT", 2000);
    if (test.indexOf("OK") != -1) {
      Serial.println("✓ Baud rate berhasil diubah ke 9600.");
    } else {
      Serial.println("⚠ Baud rate mungkin belum berubah, lanjut dengan baud sebelumnya.");
    }
  } else {
    printSeparator("STEP 2: Baud Rate OK");
    Serial.println("✓ Modem sudah di 9600 bps — tidak perlu diubah.");
  }

  // ── STEP 3: Nonaktifkan echo ────────────────────────────────────────────────
  printSeparator("STEP 3: Konfigurasi Modem");
  sendAT("ATE0");  // Echo off
  sendAT("AT+CMEE=2");  // Verbose error
  Serial.println("✓ Echo dimatikan, verbose error aktif.");

  // ── STEP 4: Cek SIM Card ─────────────────────────────────────────────────── 
  printSeparator("STEP 4: Cek SIM Card");
  String cpin = "";
  for (int i = 0; i < 5; i++) {
    cpin = sendAT("AT+CPIN?", 3000);
    if (cpin.indexOf("READY") != -1 || cpin.indexOf("ERROR") != -1) break;
    Serial.print(".");
    delay(1000);
  }

  if (cpin.indexOf("READY") != -1) {
    Serial.println("✓ SIM Card terpasang dan siap.");
  } else if (cpin.indexOf("SIM PIN") != -1) {
    Serial.println("✗ SIM Card terkunci PIN!");
  } else if (cpin.indexOf("NO SIM") != -1) {
    Serial.println("✗ SIM Card TIDAK terpasang!");
  } else {
    Serial.printf("⚠ Respons CPIN: %s\n", cpin.c_str());
  }

  // ── STEP 5: Cek Sinyal & Operator ──────────────────────────────────────────
  printSeparator("STEP 5: Sinyal & Operator");
  String csq  = sendAT("AT+CSQ",  2000);
  String cops = sendAT("AT+COPS?",2000);
  Serial.printf("Sinyal (AT+CSQ):    %s\n", csq.c_str());
  Serial.printf("Operator (AT+COPS): %s\n", cops.c_str());

  // ── STEP 6: Cek Registrasi Jaringan ─────────────────────────────────────── 
  printSeparator("STEP 6: Registrasi Jaringan");
  Serial.print("Menunggu jaringan");
  bool registered = false;
  for (int i = 0; i < 30; i++) {
    String creg = sendAT("AT+CREG?", 2000);
    if (creg.indexOf(",1") != -1) {
      Serial.println("\n✓ Terdaftar di jaringan HOME.");
      registered = true;
      break;
    } else if (creg.indexOf(",5") != -1) {
      Serial.println("\n✓ Terdaftar (ROAMING).");
      registered = true;
      break;
    }
    Serial.print(".");
    delay(1000);
  }
  if (!registered) {
    Serial.println("\n✗ Gagal registrasi jaringan dalam 30 detik.");
  }

  // ── STEP 7: Info Modem ──────────────────────────────────────────────────────
  printSeparator("STEP 7: Info Modem");
  Serial.printf("Model:    %s\n", sendAT("AT+CGMM").c_str());
  Serial.printf("Firmware: %s\n", sendAT("AT+CGMR").c_str());
  Serial.printf("IMEI:     %s\n", sendAT("AT+CGSN").c_str());

  // ── SELESAI ─────────────────────────────────────────────────────────────────
  printSeparator(registered ? "✅ DIAGNOSTIC SELESAI — SIM800L OK" 
                             : "⚠ DIAGNOSTIC SELESAI — Jaringan Gagal");
  Serial.println("Mode manual aktif.");
  Serial.println("Ketik AT command di Serial Monitor untuk lanjut testing.");
  Serial.println("Contoh: AT+SAPBR=2,1 (cek GPRS status)");
}

// =============================================================================
// HELPERS
// =============================================================================

/**
 * @brief Coba komunikasi dengan SIM800L di baud rate tertentu.
 * @return true jika modem merespons "OK".
 */
bool tryBaudRate(long baud) {
  if (sim800) sim800.end();
  sim800.begin(baud, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
  delay(300);

  // Flush buffer
  while (sim800.available()) sim800.read();

  // Kirim AT, tunggu OK
  sim800.println("AT");
  unsigned long start = millis();
  String resp = "";
  while (millis() - start < 1500) {
    while (sim800.available()) resp += (char)sim800.read();
    if (resp.indexOf("OK") != -1) return true;
    delay(10);
  }
  return false;
}

/**
 * @brief Trigger RST pin untuk reset modem (active LOW, 200ms).
 */
void resetModem() {
  Serial.println("[RST] Mereset modem via RST pin...");
  pinMode(SIM_RST_PIN, OUTPUT);
  digitalWrite(SIM_RST_PIN, HIGH);
  delay(100);
  digitalWrite(SIM_RST_PIN, LOW);
  delay(300);
  digitalWrite(SIM_RST_PIN, HIGH);
  delay(3000);  // Tunggu modem boot
  Serial.println("[RST] Reset selesai, tunggu modem siap...");
}

/**
 * @brief Kirim AT command dan kembalikan respons lengkap.
 */
String sendAT(const String &cmd, unsigned long timeoutMs) {
  while (sim800.available()) sim800.read();  // Flush
  sim800.println(cmd);

  String response = "";
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (sim800.available()) response += (char)sim800.read();
    if (response.indexOf("OK")       != -1 ||
        response.indexOf("ERROR")    != -1 ||
        response.indexOf("DOWNLOAD") != -1) break;
    delay(10);
  }
  return response;
}

/**
 * @brief Print separator dengan judul untuk keterbacaan Serial Monitor.
 */
void printSeparator(const String &title) {
  Serial.println("\n──────────────────────────────────────────");
  if (title.length() > 0) {
    Serial.println("  " + title);
    Serial.println("──────────────────────────────────────────");
  }
}
