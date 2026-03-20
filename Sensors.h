#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>

// Fungsi Inisialisasi awal untuk semua sensor
void initSensors();

// Fungsi untuk membaca dan menghitung nilai
float readPH();
float readTemperature();
int readTurbidityValue();
String getTurbidityStatus(int turbidityValue);

#endif
