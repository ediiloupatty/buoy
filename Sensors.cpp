/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Edi Loupatty
 *
 * This file is part of the Smart Buoy IoT System.
 */

/**
 * @file Sensors.cpp
 * @brief Implementation of sensor polling and calibration logic.
 */

#include "Sensors.h"
#include "Config.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <algorithm> // Required for std::sort if smoothing is ever reintroduced

// DS18B20 Temperature Sensor Instance
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

// pH Sensor Calibration Constants (Linear Equation: y = mx + b)
const float m = -4.79;
const float b = 18.06;

void initSensors() {
  // Configure ESP32 ADC attenuation for the 0-3.3V measurement range
  analogSetAttenuation(ADC_11db);
  sensors.begin();
}

float readPH() {
  // Instantaneous single-shot ADC read (smoothing removed for MCU efficiency)
  int rawAdc = analogRead(PH_PIN);
  
  // Convert 12-bit ADC value to voltage
  float voltage = rawAdc * (3.3f / 4095.0f);
  
  // Apply linear calibration equation
  return (m * voltage) + b;
}

float readTemperature() {
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  
  // Guard against hardware disconnection (-127.0 is the DS18B20 default error value)
  if (tempC <= -126.0f) {
    return -127.0f; 
  }
  return tempC;
}

int readTurbidityValue() {
  // Instantaneous single-shot ADC read
  return analogRead(TURB_PIN);
}

String getTurbidityStatus(int turbidityValue) {
  // Qualitative thresholds for water clarity
  if (turbidityValue < 1500) return "Jernih";
  else if (turbidityValue < 3000) return "Keruh";
  else return "Kotor";
}