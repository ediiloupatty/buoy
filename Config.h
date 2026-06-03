/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Edi Loupatty
 *
 * This file is part of the Smart Buoy IoT System.
 */

#ifndef CONFIG_H
#define CONFIG_H

/**
 * @file Config.h
 * @brief Global configuration constants for the Smart Buoy IoT System.
 * 
 * This file contains all hardware pin definitions, SIM800L modem settings,
 * deep sleep parameters, and Firebase endpoint configurations.
 */


/* =================================================================
 * HARDWARE PIN DEFINITIONS
 * =================================================================
 * Note: Use ADC1 pins for analog inputs to prevent conflicts with Wi-Fi operations.
 */

// Analog Sensors
#define PH_PIN   34   ///< GPIO Pin for Analog pH Sensor (ADC1)

// Digital Sensors
#define TEMP_PIN 4    ///< GPIO Pin for DS18B20 OneWire Data

// Pump Relay (Dual Channel) — RTC GPIO supaya state ke-hold saat deep sleep
#define PUMP_FILL_PIN   25   ///< GPIO → IN1 relay → Pompa ISI air
#define PUMP_DRAIN_PIN  26   ///< GPIO → IN2 relay → Pompa BUANG air
#define RELAY_ON   LOW       ///< Modul relay China umumnya active LOW (kasih LOW = relay ON)
#define RELAY_OFF  HIGH

/* ==========================================
 * PUMP CYCLE CONFIGURATION
 * ==========================================
 * Siklus OTOMATIS 3 fase berulang (mulai langsung saat ESP32 nyala):
 *   FILLING  → pompa ISI ON      (30 detik) — ambil air tambak → wadah
 *   WAITING  → kedua pompa OFF   (10 menit production) — sensor baca
 *   DRAINING → pompa BUANG ON    (30 detik) — buang air wadah → tambak
 *   → loop balik ke FILLING (otomatis tanpa intervensi)
 *
 * Aman: pompa 1 dan 2 tidak pernah ON bersamaan (interlock di state machine).
 * Mobile app hanya untuk monitoring & prediksi — tidak ada command/button.
 */

// Toggle mode: 1 = debug (timing pendek untuk testing), 0 = production (15 menit waiting)
#define PUMP_DEBUG_MODE 0
#if PUMP_DEBUG_MODE
  #define PUMP_FILL_DURATION_MS   30000UL    ///< 30 detik (debug)
  #define PUMP_WAIT_DURATION_MS   30000UL    ///< 30 detik (debug)
  #define PUMP_DRAIN_DURATION_MS  30000UL    ///< 30 detik (debug)
#else
  #define PUMP_FILL_DURATION_MS   30000UL    ///< 30 detik (production)
  #define PUMP_WAIT_DURATION_MS  600000UL    ///< 10 menit (production)
  #define PUMP_DRAIN_DURATION_MS  40000UL    ///< 40 detik (production)
#endif

// ── Telemetry: history CLOCK-ALIGNED, lepas dari siklus pompa ────────────────
// /history dikirim TEPAT pada batas jam kelipatan HISTORY_INTERVAL_SEC
// (mis. 600 dtk → menit :00, :10, :20, :30 ...), berapa pun durasi siklus pompa.
// Nilai yang dikirim = pembacaan "air tenang" terakhir yang di-cache saat WAITING.
#define HISTORY_INTERVAL_SEC     600UL     ///< Interval clock-aligned history (detik) = 10 menit
#define WIFI_RECHECK_INTERVAL_MS 60000UL   ///< Cek WiFi reconnect tiap 60 detik

// Saat fase WAITING (air tenang): endapkan dulu, lalu sampling berkala.
// Pembacaan terakhir di-cache sebagai "air tenang" untuk dikirim ke history
// pada batas jam berikutnya, dan dipakai untuk update /live real-time.
// Debug: dipercepat agar /live tetap terkirim dalam WAITING singkat (15 detik).
#if PUMP_DEBUG_MODE
  #define WAIT_SETTLE_MS           3000UL    ///< Debug: endap 3 detik
  #define WAIT_SAMPLE_INTERVAL_MS  3000UL    ///< Debug: sampling tiap 3 detik
#else
  #define WAIT_SETTLE_MS           60000UL   ///< Endap 1 menit sebelum mulai sampling
  #define WAIT_SAMPLE_INTERVAL_MS  30000UL   ///< Sampling tiap 30 detik saat WAITING
#endif

// Ambang epoch NTP dianggap valid (1 Jan 2020). Di bawah ini = NTP belum sync.
#define NTP_VALID_EPOCH          1577836800ULL

// Pump state constants (3 fase + IDLE untuk safety fallback)
#define PUMP_IDLE      0
#define PUMP_FILLING   1
#define PUMP_WAITING   2
#define PUMP_DRAINING  3

// SIM800L UART Pins
// #define SIM_RX   16   ///< GPIO Pin for SIM800L TX → ESP32 RX (UART2)
// #define SIM_TX   17   ///< GPIO Pin for SIM800L RX ← ESP32 TX (UART2)
// #define SIM_RST  5    ///< GPIO Pin for SIM800L RST (active LOW — pull LOW 200ms untuk reset)
// #define SIM_BAUD 9600 ///< SIM800L default baud rate (9600 lebih stabil dari 115200)

/* ==========================================
 * SIM800L / GPRS CONFIGURATION
 * ==========================================
 * Adjust APN according to your SIM card provider:
 *   Telkomsel : "internet"
 *   Indosat   : "indosatgprs"
 *   XL        : "internet"
 *   Tri (3)   : "3data"
 */
// static const char *APN = "internet";

/* ==========================================
 * WIFI CONFIGURATION
 * ==========================================
 */
#define WIFI_SSID "No Internet Connection"
#define WIFI_PASS "Loupatty143"

/* ==========================================
 * DEEP SLEEP CONFIGURATION
 * ==========================================
 */
#define SLEEP_DURATION_US      60000000ULL  ///< 60 detik (produksi).

/* ==========================================
 * FIREBASE CONFIGURATION
 * ==========================================
 * REST API endpoint for SIM800L HTTPS communication.
 * Format: https://<host>/<path>.json?auth=<database_secret>
 */
#define FIREBASE_HOST "monitoring-air-tambak-udang-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "hSmmOqNZ5viPULXW6RZtIU3qxtT3a2YHBM331VLW"

#endif // CONFIG_H
