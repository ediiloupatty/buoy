#ifndef CONFIG_H
#define CONFIG_H

// ==========================================
// KONFIGURASI PIN ESP32
// ==========================================
#define SDA_PIN 27
#define SCL_PIN 26
#define PH_PIN 34
#define TEMP_PIN 4
#define TURB_PIN 35

// ==========================================
// KONFIGURASI WIFI
// ==========================================
const char* ssid = "No Internet Connection";
const char* password = "Loupatty143";

// ==========================================
// KONFIGURASI FIREBASE
// ==========================================
#define FIREBASE_HOST "URL_DATABASE_FIREBASE_ANDA.firebaseio.com"
#define FIREBASE_AUTH "DATABASE_SECRET_FIREBASE_ANDA"

#endif
