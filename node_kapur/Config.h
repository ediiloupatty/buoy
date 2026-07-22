/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Edi Loupatty
 *
 * ┌───────────────────────────────────────────────────────────────────────┐
 * │  Config.h  —  NODE KAPUR / pH DISPENSER (ESP8266 + LoRa RX + Servo)   │
 * └───────────────────────────────────────────────────────────────────────┘
 *
 * Node ini MENDENGAR paket "CMD|K|<doseId>|<openMs>|<seq>" dari buoy. Setiap
 * doseId yang NAIK = perintah menabur 1 dosis kapur: servo membuka selama
 * openMs lalu menutup lagi. doseId yang sama (re-broadcast) TIDAK menabur ulang.
 *
 * ⚠ Blok LORA PARAMETERS harus SAMA PERSIS dengan node lain.
 */

#ifndef CONFIG_H
#define CONFIG_H

/* =================================================================
 * PIN LoRa (Ra-02 SX1278) pada ESP8266
 * =================================================================
 * SPI fix: SCK=D5(GPIO14), MISO=D6(GPIO12), MOSI=D7(GPIO13).
 */
#define LORA_SS    15   ///< D8 → NSS
#define LORA_RST   16   ///< D0 → RST
#define LORA_DIO0   5   ///< D1 → DIO0

/* =================================================================
 * PIN & sudut Servo dispenser
 * =================================================================
 * Servo SG90/MG90 disarankan diberi catu 5V terpisah (bukan dari 3V3 ESP8266),
 * dengan GND disatukan. Sinyal PWM dari GPIO 3V3 sudah cukup untuk SG90.
 */
#define SERVO_PIN         4    ///< D2 (GPIO4) → sinyal servo
#define SERVO_CLOSED_DEG  0    ///< posisi hopper TERTUTUP
#define SERVO_OPEN_DEG    90   ///< posisi hopper TERBUKA (kalibrasi sesuai mekanik)

// Kecepatan sapuan servo saat buka/tutup — makin besar STEP_MS makin pelan.
// 1° per 40 ms ≈ 3,6 detik untuk 90° (dibanding gerak penuh SG90 ±150 ms).
#define SERVO_SWEEP_STEP_DEG  1     ///< derajat per langkah sapuan
#define SERVO_SWEEP_STEP_MS   40UL  ///< jeda antar langkah (ms)

/* =================================================================
 * LORA PARAMETERS  ⚠ HARUS SAMA DI SEMUA NODE ⚠
 * ================================================================= */
#define LORA_FREQUENCY          433E6
#define LORA_SYNC_WORD          0x12
#define LORA_SPREADING_FACTOR   9
#define LORA_BANDWIDTH          125E3

#define LORA_CMD_PREFIX  "CMD"
#define CMD_DST_KAPUR    'K'   ///< node ini hanya bereaksi pada tujuan 'K'

/* =================================================================
 * BATAS AMAN durasi buka servo
 * =================================================================
 * Jaga-jaga bila openMs dari paket korup/terlalu besar → hopper tidak terkunci
 * terbuka berlama-lama. Clamp ke nilai ini.
 */
#define DOSE_OPEN_MAX_MS  90000UL  ///< maksimum 90 detik per dosis (dosis normal 1 menit)

#endif // CONFIG_H
