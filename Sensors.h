/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Edi Loupatty
 *
 * This file is part of the Smart Buoy IoT System.
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>

/**
 * @file Sensors.h
 * @brief Hardware abstraction layer for water quality sensors.
 *
 * Defines the public interfaces for initializing and polling 
 * telemetry data from the connected analog and digital sensors.
 */

/**
 * @brief Initializes all connected sensors and configures ADC attenuation.
 */
void initSensors();

/**
 * @brief Loads calibration constants (m, b) from NVS flash memory.
 * Falls back to default values if no calibration data exists.
 * Must be called once during setup(), after initSensors().
 */
void loadCalibration();

/**
 * @brief Processes a serial calibration command.
 * 
 * Supported commands:
 * - "CAL4"    : Record voltage at pH 4.01 buffer
 * - "CAL7"    : Record voltage at pH 6.86 buffer
 * - "CALSAVE" : Calculate & persist m/b to NVS
 * - "CALINFO" : Print current calibration status
 * 
 * @param cmd The command string received from Serial input.
 */
void handleCalibrationCommand(String cmd);

/**
 * @brief Reads and calibrates the current pH value from the analog sensor.
 * @return Calculated pH value (float) without temperature compensation.
 */
float readPH();

/**
 * @brief Reads and calibrates the current pH value with temperature compensation.
 * Applies Nernst-based correction using the provided water temperature.
 * @param tempC Current water temperature in Celsius from DS18B20.
 * @return Temperature-compensated pH value (float).
 */
float readPH(float tempC);

/**
 * @brief Requests and retrieves the current water temperature via OneWire.
 * @return Temperature in Celsius (float). Returns -127.0f on sensor fault.
 */
float readTemperature();

/**
 * @brief Reads the raw ADC value from the turbidity sensor.
 * @return Raw ADC integer value (0-4095).
 */
int readTurbidityValue();

/**
 * @brief Computes a qualitative state based on the raw turbidity value.
 * @param turbidityValue The raw ADC value from the turbidity sensor.
 * @return String representing the qualitative state ("Jernih", "Keruh", "Kotor").
 */
String getTurbidityStatus(int turbidityValue);

#endif // SENSORS_H
