#ifndef CONFIG_H
#define CONFIG_H

/* =================================================================
 * PIN LoRa (Ra-02 SX1278) pada ESP8266 (Mock Transmitter)
 * =================================================================
 * Sesuai dengan ESP8266 pada Node Kapur.
 */
#define LORA_SS    15   
#define LORA_RST   16   
#define LORA_DIO0   5   

/* =================================================================
 * LORA PARAMETERS  ⚠ HARUS SAMA DENGAN NODE KAPUR ⚠
 * ================================================================= */
#define LORA_FREQUENCY          433E6
#define LORA_SYNC_WORD          0x12
#define LORA_SPREADING_FACTOR   9
#define LORA_BANDWIDTH          125E3

#define LORA_CMD_PREFIX  "CMD"
#define CMD_DST_KAPUR    'K'

#endif // CONFIG_H
