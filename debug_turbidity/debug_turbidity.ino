/*
 * Sketch debug untuk diagnosa sensor turbidity.
 * Loop terus, cetak voltase mentah ke Serial agar bisa membandingkan
 * respons sensor saat dicelup ke 3 larutan (jernih / sedang / keruh).
 *
 * Cara pakai:
 *   1. Upload sketch ini ke ESP32 (papan & port yang sama dengan final_project).
 *   2. Buka Serial Monitor pada 115200 baud.
 *   3. Celupkan sensor ke larutan, tunggu 10-15 detik agar pembacaan stabil.
 *   4. Catat baris [TRIMMED V] dan [ADC] saat di setiap larutan.
 *
 * Interpretasi cepat:
 *   - Voltase berubah signifikan antar larutan -> sensor sehat, mungkin kalibrasi off.
 *   - Voltase nyaris sama walau larutan berbeda -> modul sensor degradasi / optikal buram.
 *   - ADC range (max-min) > 200 -> noise berlebihan, bisa karena power supply / kabel.
 */

#include <Arduino.h>

#define TURB_PIN 35   // Sama dengan Config.h pada final_project

const int SAMPLES = 20;
const int SAMPLE_DELAY_MS = 5;
const unsigned long PRINT_INTERVAL_MS = 500;

// Nilai kalibrasi tersimpan (untuk perbandingan visual saja)
const float REF_V0_NTU   = 1.4633f;
const float REF_V70_NTU  = 1.2400f;
const float REF_V400_NTU = 1.0086f;

float lastVoltage = -1.0f;

void setup() {
  Serial.begin(115200);
  delay(200);

  analogSetAttenuation(ADC_11db);  // Range 0-3.3V

  Serial.println();
  Serial.println("=========================================");
  Serial.println("  DEBUG TURBIDITY - Live Voltage Monitor");
  Serial.println("=========================================");
  Serial.printf("Pin            : GPIO %d (ADC1)\n", TURB_PIN);
  Serial.printf("ADC Resolution : 12-bit (0-4095)\n");
  Serial.printf("Voltage Range  : 0.0 - 3.3 V (ADC_11db)\n");
  Serial.printf("Samples/read   : %d (delay %d ms)\n", SAMPLES, SAMPLE_DELAY_MS);
  Serial.println("-----------------------------------------");
  Serial.println("Referensi kalibrasi tersimpan:");
  Serial.printf("  V(0   NTU) = %.4f V  (jernih)\n", REF_V0_NTU);
  Serial.printf("  V(70  NTU) = %.4f V  (sedang)\n", REF_V70_NTU);
  Serial.printf("  V(400 NTU) = %.4f V  (keruh)\n", REF_V400_NTU);
  Serial.println("=========================================");
  Serial.println("Format output: [ADC min/max/avg] [V trimmed] [delta]\n");
}

void loop() {
  int samples[SAMPLES];

  // Ambil sample mentah
  for (int i = 0; i < SAMPLES; i++) {
    samples[i] = analogRead(TURB_PIN);
    delay(SAMPLE_DELAY_MS);
  }

  // Sort untuk trimmed mean & cari min/max
  for (int i = 1; i < SAMPLES; i++) {
    int key = samples[i];
    int j = i - 1;
    while (j >= 0 && samples[j] > key) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = key;
  }

  int rawMin = samples[0];
  int rawMax = samples[SAMPLES - 1];
  int rawRange = rawMax - rawMin;

  // Trimmed mean: buang 4 terendah & 4 tertinggi
  const int TRIM = 4;
  long sum = 0;
  for (int i = TRIM; i < SAMPLES - TRIM; i++) sum += samples[i];
  float avgAdc = (float)sum / (SAMPLES - 2 * TRIM);
  float voltage = avgAdc * (3.3f / 4095.0f);

  // Hitung delta dari pembacaan sebelumnya
  float delta = (lastVoltage < 0) ? 0.0f : (voltage - lastVoltage);
  lastVoltage = voltage;

  // Tandai larutan terdekat berdasarkan voltase
  const char* dekat = "?";
  float minDist = 9999.0f;
  if (fabs(voltage - REF_V0_NTU)   < minDist) { minDist = fabs(voltage - REF_V0_NTU);   dekat = "~jernih(0)"; }
  if (fabs(voltage - REF_V70_NTU)  < minDist) { minDist = fabs(voltage - REF_V70_NTU);  dekat = "~sedang(70)"; }
  if (fabs(voltage - REF_V400_NTU) < minDist) { minDist = fabs(voltage - REF_V400_NTU); dekat = "~keruh(400)"; }

  Serial.printf("[ADC min=%4d max=%4d range=%3d avg=%6.1f]  [V=%.4f]  [delta=%+.4f V]  %s\n",
                rawMin, rawMax, rawRange, avgAdc, voltage, delta, dekat);

  // Peringatan noise
  if (rawRange > 200) {
    Serial.println("  -> WARNING: ADC range besar (>200), cek power supply / kabel.");
  }

  delay(PRINT_INTERVAL_MS);
}
