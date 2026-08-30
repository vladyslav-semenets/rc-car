#pragma once
#include <Arduino.h>
#include <SPI.h>

// ─── Official Seeed Studio XIAO ESP32-S3 + Wio-SX1262 B2B Pinout ──────────────
#define LORA_SCK            7     // SPI Clock -> GPIO 7
#define LORA_MISO           8     // SPI MISO -> GPIO 8
#define LORA_MOSI           9     // SPI MOSI -> GPIO 9

#define LORA_NSS            41    // Chip Select (NSS) -> GPIO 41
#define LORA_DIO1           39    // Interrupt (DIO1) -> GPIO 39
#define LORA_NRST           42    // Reset (NRST) -> GPIO 42
#define LORA_BUSY           40    // Busy -> GPIO 40
#define LORA_RF_SWITCH      38    // RF Antenna Switch -> GPIO 38

// Onboard user LED on XIAO ESP32-S3 (active LOW)
#ifndef LED_PIN
#define LED_PIN             21    // GPIO 21 (LED_BUILTIN on XIAO S3)
#endif

// ─── LoRa RF Configuration (868 MHz EU ISM Band) ──────────────────────────────
#define LORA_FREQUENCY          868.0     // MHz
#define LORA_BANDWIDTH          500.0     // kHz (high speed / low airtime)
#define LORA_SPREADING_FACTOR   7         // SF7 (fast, low airtime ~8ms)
#define LORA_CODING_RATE        5         // 4/5
#define LORA_SYNC_WORD          0x12      // Private network sync word
#define LORA_OUTPUT_POWER       22        // dBm (max power for SX1262)
#define LORA_PREAMBLE_LENGTH    8         // symbols
#define LORA_TCXO_VOLTAGE       1.6       // V (Wio-SX1262 TCXO)

// ─── Bluetooth Low Energy (BLE) Nordic UART Service (NUS) Configuration ───────
#define BLE_DEVICE_NAME         "RCCAR-GROUND-TX"
#define BLE_NUS_SERVICE_UUID    "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_NUS_RX_CHAR_UUID    "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // Write from Phone -> ESP32
#define BLE_NUS_TX_CHAR_UUID    "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // Notify ESP32 -> Phone

#define MAX_PACKET_SIZE         256
#define SERIAL_BAUD_RATE        115200
