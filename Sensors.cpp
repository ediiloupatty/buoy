#include "Sensors.h"
#include "Config.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <algorithm> // Required for std::sort

// Setup Sensor Suhu DS18B20
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

// pH Calibration Variables (Linear Equation: y = mx + b)
const float m = -4.79;
const float b = 18.06;

void initSensors() {
  // Use 11dB attenuation for 0-3.3V range on ESP32
  analogSetAttenuation(ADC_11db);
  sensors.begin();
}

/**
 * Helper function to calculate the Median of an array.
 * This filters out high/low noise spikes which are common in water sensors.
 */
float getMedian(int samples[], int n) {
  std::sort(samples, samples + n);
  if (n % 2 == 0) {
    // Explicit cast to float to prevent integer division warnings
    return (float)(samples[n / 2 - 1] + samples[n / 2]) / 2.0f;
  }
  return (float)samples[n / 2];
}

float readPH() {
  int samples[20];
  for (int i = 0; i < 20; i++) {
    samples[i] = (int)analogRead(PH_PIN);
    delay(15); 
  }
  
  float medianAdc = getMedian(samples, 20);
  float voltage = medianAdc * (3.3f / 4095.0f);
  return (m * voltage) + b;
}

float readTemperature() {
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  
  // -127.0 is the standard value for DS18B20 disconnection
  if (tempC <= -126.0f) {
    return -127.0f; 
  }
  return tempC;
}

int readTurbidityValue() {
  int samples[15];
  for (int i = 0; i < 15; i++) {
    samples[i] = (int)analogRead(TURB_PIN);
    delay(10);
  }
  return (int)getMedian(samples, 15);
}

String getTurbidityStatus(int turbidityValue) {
  if (turbidityValue < 1500) return "Jernih";
  else if (turbidityValue < 3000) return "Keruh";
  else return "Kotor";
}


