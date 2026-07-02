#ifndef CONFIG_H
#define CONFIG_H

/* =================================================================
 * PIN LoRa (Ra-02 SX1278) pada ESP8266 (Mock Buoy Transmitter)
 * =================================================================
 * Sama dengan wiring pada node aktuator (NodeMCU / Wemos D1 mini).
 */
#define LORA_SS    15
#define LORA_RST   16
#define LORA_DIO0   5

/* =================================================================
 * LORA PARAMETERS  ⚠ HARUS SAMA DENGAN NODE POMPA & NODE KAPUR ⚠
 * ================================================================= */
#define LORA_FREQUENCY          433E6
#define LORA_SYNC_WORD          0x12
#define LORA_SPREADING_FACTOR   9
#define LORA_BANDWIDTH          125E3

#define LORA_CMD_PREFIX  "CMD"
#define CMD_DST_PUMP     'P'
#define CMD_DST_KAPUR    'K'

/* =================================================================
 * PERILAKU — samakan dengan buoy asli (buoy_transmitter/Config.h)
 * ================================================================= */
#define DOSE_SERVO_OPEN_MS  1500UL    ///< lama servo membuka per dosis (ms)
#define CMD_REBROADCAST_MS  15000UL   ///< ulang kirim state aktuator tiap 15 detik

/* =================================================================
 * MODE AUTO — siklus pompa otomatis (jalan tanpa Serial Monitor,
 * misal saat TX ditenagai charger HP)
 * ================================================================= */
#define AUTO_PUMP_ON_MS   15000UL   ///< pompa NYALA selama 15 detik
#define AUTO_PUMP_OFF_MS   5000UL   ///< pompa MATI selama 5 detik

#endif // CONFIG_H
