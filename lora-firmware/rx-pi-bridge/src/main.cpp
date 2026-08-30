#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include "config.h"

// Instantiate SX1262 module with Seeed XIAO ESP32-S3 B2B pin mapping
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);

// Packet buffers
static uint8_t rxBuffer[MAX_PACKET_SIZE];
static uint8_t txBuffer[MAX_PACKET_SIZE];

// Interrupt flags
static volatile bool packetReceived = false;
static volatile bool isTransmitting = false;

// Statistics & LED timing
static uint32_t lastLedBlinkMs = 0;
static uint32_t rxPacketCount = 0;
static uint32_t txPacketCount = 0;

#if defined(ESP32)
IRAM_ATTR
#endif
static void onDio1Action(void) {
    packetReceived = true;
}

static void blinkLed(void) {
    digitalWrite(LED_PIN, LOW); // Active LOW on XIAO
    lastLedBlinkMs = millis();
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // OFF

    // Initialize USB CDC Serial
    Serial.begin(SERIAL_BAUD_RATE);

    uint32_t startMs = millis();
    while (!Serial && (millis() - startMs < 2000)) {
        delay(10);
    }

    Serial.println(F("========================================"));
    Serial.println(F("  RC Car Rover RX (LoRa 868 -> USB CDC)"));
    Serial.println(F("========================================"));

    // ── 1. Initialize SPI & RF Switch Pins ────────────────────────────────────
    Serial.println(F("[Rover RX Bridge] Initializing SPI bus (SCK=7, MISO=8, MOSI=9, NSS=41)..."));
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

    pinMode(LORA_RF_SWITCH, OUTPUT);
    digitalWrite(LORA_RF_SWITCH, LOW);

    // Set RF switch pin on SX1262
    radio.setRfSwitchPins(LORA_RF_SWITCH, RADIOLIB_NC);

    // ── 2. Initialize SX1262 ──────────────────────────────────────────────────
    Serial.println(F("[Rover RX Bridge] Initializing SX1262 (NSS=41, DIO1=39, NRST=42, BUSY=40, RF_SW=38)..."));

    int state = radio.begin(
        LORA_FREQUENCY,
        LORA_BANDWIDTH,
        LORA_SPREADING_FACTOR,
        LORA_CODING_RATE,
        LORA_SYNC_WORD,
        LORA_OUTPUT_POWER,
        LORA_PREAMBLE_LENGTH,
        LORA_TCXO_VOLTAGE
    );

    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[Rover RX Bridge] TCXO 1.6V initialization failed (%d), retrying with no TCXO...\n", state);
        state = radio.begin(
            LORA_FREQUENCY,
            LORA_BANDWIDTH,
            LORA_SPREADING_FACTOR,
            LORA_CODING_RATE,
            LORA_SYNC_WORD,
            LORA_OUTPUT_POWER,
            LORA_PREAMBLE_LENGTH,
            0.0
        );
    }

    if (state == RADIOLIB_ERR_NONE) {
        Serial.println(F("[Rover RX Bridge] SUCCESS! SX1262 ready on 868.0 MHz (SF7, BW500k, 22dBm)"));
    } else {
        Serial.printf("[Rover RX Bridge] FATAL: Initialization failed with error code %d!\n", state);
        while (true) {
            digitalWrite(LED_PIN, LOW);
            delay(150);
            digitalWrite(LED_PIN, HIGH);
            delay(150);
        }
    }

    // Set DIO1 interrupt for packet reception
    radio.setPacketReceivedAction(onDio1Action);

    // Start listening for incoming LoRa packets
    state = radio.startReceive();
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[Rover RX Bridge] startReceive failed, code: %d\n", state);
    } else {
        Serial.println(F("[Rover RX Bridge] Listening on 868.0 MHz (SF7, BW500k)..."));
    }
}

void loop() {
    // ── Turn off activity LED after 20 ms ─────────────────────────────────────
    if (lastLedBlinkMs > 0 && (millis() - lastLedBlinkMs > 20)) {
        digitalWrite(LED_PIN, HIGH);
        lastLedBlinkMs = 0;
    }

    // ── 1. Uplink: LoRa RX -> USB CDC Serial (to Raspberry Pi) ────────────────
    if (packetReceived && !isTransmitting) {
        packetReceived = false;

        size_t len = radio.getPacketLength();
        if (len > 0 && len <= MAX_PACKET_SIZE) {
            int state = radio.readData(rxBuffer, len);

            if (state == RADIOLIB_ERR_NONE) {
                Serial.write(rxBuffer, len);
                Serial.flush();
                rxPacketCount++;
                blinkLed();
            }
        }

        // Resume listening on LoRa
        radio.startReceive();
    }

    // ── 2. Downlink: USB CDC Serial -> LoRa TX (Telemetry to Ground TX) ───────
    if (Serial.available()) {
        size_t txLen = 0;
        uint32_t serialTimeout = millis();

        // Read available chunk (framing timeout: 3 ms after last byte)
        while (millis() - serialTimeout < 3 && txLen < MAX_PACKET_SIZE) {
            if (Serial.available()) {
                txBuffer[txLen++] = Serial.read();
                serialTimeout = millis();
            }
        }

        if (txLen > 0) {
            isTransmitting = true;
            radio.transmit(txBuffer, txLen);
            txPacketCount++;
            blinkLed();
            isTransmitting = false;

            // Re-arm receiver
            radio.startReceive();
        }
    }
}
