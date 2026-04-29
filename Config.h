#ifndef CONFIG_H
#define CONFIG_H

// =================================================================
// KONFIGURASI PIN FISIK (TRANSFORMASI KE BOARD LABEL "P")
// =================================================================
// Hubungkan kabel data sensor ke label "P" yang sesuai di board Anda.
// =================================================================

// 1. PIN ANALOG (Gunakan ADC1 agar tidak bentrok dengan Wi-Fi)
#define PH_PIN   34   // Fisik: Cari label "P34" atau "34"
#define TURB_PIN 35   // Fisik: Cari label "P35" atau "35"

// 2. PIN DIGITAL (OneWire untuk Suhu)
#define TEMP_PIN 4    // Fisik: Cari label "P4" atau "4"

// ==========================================
// KONFIGURASI WIFI
// ==========================================
static const char *ssid = "No Internet Connection";
static const char *password = "Loupatty143";

// ==========================================
// KONFIGURASI FIREBASE (untuk Flutter real-time dashboard)
// ==========================================
#define FIREBASE_HOST "monitoring-air-tambak-udang-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "hSmmOqNZ5viPULXW6RZtIU3qxtT3a2YHBM331VLW"

// ==========================================
// KONFIGURASI SUPABASE (untuk tabel PostgreSQL / ML Training)
// ==========================================
// Project: Supabase PostgreSQL — tabel sensor_readings
#define SUPABASE_URL  "https://glksldrpehwhzjwwlepj.supabase.co"
#define SUPABASE_KEY  "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Imdsa3NsZHJwZWh3aHpqd3dsZXBqIiwicm9sZSI6ImFub24iLCJpYXQiOjE3Nzc0Njg5MDcsImV4cCI6MjA5MzA0NDkwN30.amtlC2MzTz9JvVaECjeca82JzkfZaBvDshjrhtaXAlI"

#endif
