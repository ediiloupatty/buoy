/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Edi Loupatty
 *
 * ┌───────────────────────────────────────────────────────────────────────┐
 * │  Config.h  —  NODE POMPA COOLING (ESP8266 + LoRa RX + Relay)          │
 * └───────────────────────────────────────────────────────────────────────┘
 *
 * Node ini MENDENGAR paket "CMD|P|<state>|0|<seq>" dari buoy lalu menyalakan/
 * mematikan relay pompa cooling. Bila link LoRa putus → fail-safe OFF.
 *
 * ⚠ Blok LORA PARAMETERS harus SAMA PERSIS dengan node lain.
 */

#ifndef CONFIG_H
#define CONFIG_H

/* =================================================================
 * PIN LoRa (Ra-02 SX1278) pada ESP8266 (NodeMCU / Wemos D1 mini)
 * =================================================================
 * SPI ESP8266 itu FIX: SCK=D5(GPIO14), MISO=D6(GPIO12), MOSI=D7(GPIO13).
 * Hanya NSS/RST/DIO0 yang bebas dipilih (di-set via LoRa.setPins()).
 */
#define LORA_SS    15   ///< D8  → NSS
#define LORA_RST   16   ///< D0  → RST
#define LORA_DIO0   5   ///< D1  → DIO0

/* =================================================================
 * PIN Relay pompa cooling
 * =================================================================
 * Modul relay ini ternyata ACTIVE-HIGH (IN diberi HIGH → relay nyala) — terbukti
 * dari uji: state ON justru mematikan pompa saat masih active-LOW. Untuk mencegah
 * relay "klik ON" sekejap saat boot, beri pull-DOWN eksternal 10kΩ ke GND pada
 * jalur IN, dan kode meng-OFF-kan relay paling awal di setup().
 * (Kalau ganti modul yang active-LOW, tukar balik RELAY_ON/RELAY_OFF di bawah.)
 */
#define RELAY_PIN   4    ///< D2 (GPIO4) → IN relay
#define RELAY_ON    HIGH ///< modul active-HIGH: HIGH = pompa nyala
#define RELAY_OFF   LOW

/* =================================================================
 * LED indikator link (LED biru bawaan board, active-LOW)
 * =================================================================
 * Berkedip singkat setiap paket CMD valid diterima dari buoy/debug TX.
 * LED diam = tidak ada paket masuk (cek TX / jarak / antena).
 */
#define LED_LINK_PIN      2       ///< D4 (GPIO2) = LED biru bawaan
#define LED_LINK_ON       LOW
#define LED_LINK_OFF      HIGH
#define LED_BLINK_MS      120UL   ///< lama kedip per paket (ms)

/* =================================================================
 * LORA PARAMETERS  ⚠ HARUS SAMA DI SEMUA NODE ⚠
 * ================================================================= */
#define LORA_FREQUENCY          433E6
#define LORA_SYNC_WORD          0x12
#define LORA_SPREADING_FACTOR   9
#define LORA_BANDWIDTH          125E3

#define LORA_CMD_PREFIX  "CMD"
#define CMD_DST_PUMP     'P'   ///< node ini hanya bereaksi pada tujuan 'P'

/* =================================================================
 * FAIL-SAFE
 * =================================================================
 * Buoy re-broadcast perintah tiap ~15 detik. Bila > ACT_FAILSAFE_MS tidak ada
 * perintah valid untuk node ini → anggap link putus → matikan pompa (aman).
 */
#define ACT_FAILSAFE_MS  90000UL   ///< 90 detik

#endif // CONFIG_H
