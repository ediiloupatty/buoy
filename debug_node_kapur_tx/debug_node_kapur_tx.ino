/*
 * @file debug_node_kapur_tx.ino
 * @brief Mock Transmitter untuk debugging Node Kapur
 * 
 * Upload ini ke ESP8266 ke-2 Anda. 
 * Buka Serial Monitor (baudrate 115200), lalu ketik 't' dan tekan Enter 
 * untuk mensimulasikan pengiriman perintah LoRa ke Node Kapur.
 */

#include <SPI.h>
#include <LoRa.h>
#include "Config.h"

uint32_t currentDoseId = 1;
uint32_t seq = 1;

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n══════════════════════════════════════════");
  Serial.println("  DEBUG TRANSMITTER untuk NODE KAPUR");
  Serial.println("══════════════════════════════════════════");

  // Inisialisasi LoRa
  SPI.begin();
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("[LoRa] ✗ Init GAGAL — cek wiring modul LoRa.");
    while (1); // Berhenti di sini jika gagal
  }

  // Set parameter LoRa agar sama persis dengan penerima
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.enableCrc();

  Serial.println("[LoRa] ✓ TX Siap.");
  Serial.println("\n[INFO] Cara penggunaan:");
  Serial.println("Ketik huruf 't' lalu tekan Enter di Serial Monitor");
  Serial.println("untuk mengirim perintah 'tabur kapur'.\n");
}

void loop() {
  unsigned long openMs = 1000; // Buka servo 1 detik
  
  // Format payload: CMD|K|<doseId>|<openMs>|<seq>
  String payload = String(LORA_CMD_PREFIX) + "|" + 
                   CMD_DST_KAPUR + "|" + 
                   currentDoseId + "|" + 
                   openMs + "|" + 
                   seq;
  
  Serial.print(">> Mengirim LoRa otomatis: ");
  Serial.println(payload);

  // Kirim paket LoRa
  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();
  
  Serial.println(">> Terkirim!");
  
  currentDoseId++;
  seq++;

  // Jeda 3 detik sebelum mengirim ulang otomatis
  delay(3000);
}
