#include "Sensors.h"
#include "Config.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// Setup Sensor Suhu DS18B20
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

// Variabel Kalibrasi pH (Tetap sesuai standar awal dan kesepakatan)
const float m = -4.79;
const float b = 18.06;

void initSensors() {
  analogSetAttenuation(ADC_11db);
  sensors.begin();
}

float readPH() {
  int total = 0;
  for (int i = 0; i < 20; i++) {
    total += analogRead(PH_PIN);
    delay(10);
  }
  float adc = total / 20.0;
  float voltage = adc * (3.3 / 4095.0);
  return (m * voltage) + b;
}

float readTemperature() {
  sensors.requestTemperatures();
  return sensors.getTempCByIndex(0);
}

int readTurbidityValue() {
  int total = 0;
  for (int i = 0; i < 10; i++) {
    total += analogRead(TURB_PIN);
    delay(5);
  }
  return total / 10;
}

String getTurbidityStatus(int turbidityValue) {
  if (turbidityValue < 1500) return "Jernih";
  else if (turbidityValue < 3000) return "Keruh";
  else return "Kotor";
}
