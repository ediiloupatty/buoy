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
 * @brief Reads and calibrates the current pH value from the analog sensor.
 * @return Calculated pH value (float).
 */
float readPH();

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
