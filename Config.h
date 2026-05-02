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
#define TURB_PIN 35   ///< GPIO Pin for Analog Turbidity Sensor (ADC1)

// Digital Sensors
#define TEMP_PIN 4    ///< GPIO Pin for DS18B20 OneWire Data

// SIM800L UART Pins
#define SIM_RX   16   ///< GPIO Pin for SIM800L TX → ESP32 RX (UART2)
#define SIM_TX   17   ///< GPIO Pin for SIM800L RX ← ESP32 TX (UART2)
#define SIM_BAUD 9600 ///< SIM800L default baud rate

/* ==========================================
 * SIM800L / GPRS CONFIGURATION
 * ==========================================
 * Adjust APN according to your SIM card provider:
 *   Telkomsel : "internet"
 *   Indosat   : "indosatgprs"
 *   XL        : "internet"
 *   Tri (3)   : "3data"
 */
static const char *APN = "internet";

/* ==========================================
 * DEEP SLEEP CONFIGURATION
 * ==========================================
 */
#define SLEEP_DURATION_US      120000000ULL  ///< 2 minutes in microseconds (120s × 1,000,000)
#define HISTORY_EVERY_N_BOOTS  5             ///< Push history every 5 boots (5 × 2 min = 10 min)

/* ==========================================
 * FIREBASE CONFIGURATION
 * ==========================================
 * REST API endpoint for SIM800L HTTPS communication.
 * Format: https://<host>/<path>.json?auth=<database_secret>
 */
#define FIREBASE_HOST "monitoring-air-tambak-udang-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "hSmmOqNZ5viPULXW6RZtIU3qxtT3a2YHBM331VLW"

#endif // CONFIG_H
