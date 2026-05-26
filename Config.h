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
#define TURB_PIN 35   ///<  GPIO Pin for Analog Turbidity Sensor (ADC1)

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
 *   FILLING  → pompa ISI ON      (1 menit) — ambil air tambak → wadah
 *   WAITING  → kedua pompa OFF   (15 detik debug / 15 menit production) — sensor baca
 *   DRAINING → pompa BUANG ON    (40 detik) — buang air wadah → tambak
 *   → loop balik ke FILLING (otomatis tanpa intervensi)
 *
 * Aman: pompa 1 dan 2 tidak pernah ON bersamaan (interlock di state machine).
 * Mobile app hanya untuk monitoring & prediksi — tidak ada command/button.
 */

// Toggle mode: 1 = debug (timing pendek untuk testing), 0 = production (15 menit waiting)
#define PUMP_DEBUG_MODE  1

#if PUMP_DEBUG_MODE
  #define PUMP_FILL_DURATION_MS   60000UL    ///< 1 menit
  #define PUMP_WAIT_DURATION_MS   15000UL    ///< 15 detik (debug)
  #define PUMP_DRAIN_DURATION_MS  40000UL    ///< 40 detik
#else
  #define PUMP_FILL_DURATION_MS   60000UL    ///< 1 menit
  #define PUMP_WAIT_DURATION_MS  900000UL    ///< 15 menit (production)
  #define PUMP_DRAIN_DURATION_MS  40000UL    ///< 40 detik
#endif

// Interval kirim sensor data ke Firebase (ESP32 always-on, tidak deep sleep)
#define PUMP_TELEMETRY_INTERVAL_MS  30000UL  ///< Kirim sensor data tiap 30 detik
#define WIFI_RECHECK_INTERVAL_MS    60000UL  ///< Cek WiFi reconnect tiap 60 detik

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
#define HISTORY_EVERY_N_BOOTS  10            ///< Push history every 10 boots (10 × 1 min = 10 min)

/* ==========================================
 * FIREBASE CONFIGURATION
 * ==========================================
 * REST API endpoint for SIM800L HTTPS communication.
 * Format: https://<host>/<path>.json?auth=<database_secret>
 */
#define FIREBASE_HOST "monitoring-air-tambak-udang-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "hSmmOqNZ5viPULXW6RZtIU3qxtT3a2YHBM331VLW"

#endif // CONFIG_H
