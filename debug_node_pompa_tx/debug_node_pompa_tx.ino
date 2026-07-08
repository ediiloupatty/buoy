#include <SPI.h>
#include <LoRa.h>
#include "Config.h"

uint32_t seq = 1;
bool pumpState = false; // Toggle ON/OFF bergantian

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n══════════════════════════════════════════");
  Serial.println("  DEBUG TRANSMITTER untuk NODE POMPA");
  Serial.println("══════════════════════════════════════════");

  SPI.begin();
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("[LoRa] ✗ Init GAGAL — cek wiring modul LoRa.");
    while (1);
  }

  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.enableCrc();

  Serial.println("[LoRa] ✓ TX Siap.");
  Serial.println("\n[INFO] Cara penggunaan:");
  Serial.println("Ketik huruf 't' lalu tekan Enter di Serial Monitor");
  Serial.println("untuk mengubah status Relay Pompa (ON/OFF) via LoRa.\n");
}

void loop() {
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 't' || c == 'T') {
      pumpState = !pumpState; // Ganti state dari OFF ke ON, atau sebaliknya
      int stateVal = pumpState ? 1 : 0;
      
      // Format payload: CMD|P|<state>|0|<seq>
      String payload = String(LORA_CMD_PREFIX) + "|" + 
                       CMD_DST_PUMP + "|" + 
                       stateVal + "|0|" + 
                       seq;
      
      Serial.printf("[Serial] Karakter '%c' diterima -> Mengirim LoRa (Pompa %s): %s\n", c, pumpState ? "ON" : "OFF", payload.c_str());

      LoRa.beginPacket();
      LoRa.print(payload);
      LoRa.endPacket();
      
      Serial.println(">> Terkirim!");
      
      seq++;
    }
  }
  delay(10);
}
