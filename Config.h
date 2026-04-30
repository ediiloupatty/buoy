#ifndef CONFIG_H
#define CONFIG_H

/**
 * @file Config.h
 * @brief Global configuration constants for the Smart Buoy IoT System.
 * 
 * This file contains all hardware pin definitions, network credentials, 
 * and Firebase endpoint configurations. Ensure credentials are not 
 * exposed in public repositories.
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

/* ==========================================
 * NETWORK CREDENTIALS
 * ==========================================
 */
static const char *ssid = "No Internet Connection";
static const char *password = "Loupatty143";

/* ==========================================
 * FIREBASE CONFIGURATION
 * ==========================================
 * Required for real-time telemetry streaming to the Flutter client.
 */
#define FIREBASE_HOST "monitoring-air-tambak-udang-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "hSmmOqNZ5viPULXW6RZtIU3qxtT3a2YHBM331VLW"

#endif // CONFIG_H
