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



float readPH() {
  // Baca raw ADC sekali jalan (tanpa smoothing/loop)
  int rawAdc = analogRead(PH_PIN);
  
  float voltage = rawAdc * (3.3f / 4095.0f);
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
  // Baca raw ADC sekali jalan (tanpa smoothing/loop)
  return analogRead(TURB_PIN);
}

String getTurbidityStatus(int turbidityValue) {
  if (turbidityValue < 1500) return "Jernih";
  else if (turbidityValue < 3000) return "Keruh";
  else return "Kotor";
}


