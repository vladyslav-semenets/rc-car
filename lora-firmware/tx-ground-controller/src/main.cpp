#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <NimBLEDevice.h>
#include "config.h"

// Instantiate SX1262 module with Seeed XIAO ESP32-S3 B2B pin mapping
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);

// BLE Server & Characteristics
static NimBLEServer* pServer = nullptr;
static NimBLECharacteristic* pTxCharacteristic = nullptr;
static NimBLECharacteristic* pRxCharacteristic = nullptr;
static volatile bool isBleConnected = false;

// Packet Ring Buffer for Uplink (BLE -> LoRa)
#define QUEUE_CAPACITY 8
struct UplinkPacket {
    uint8_t data[MAX_PACKET_SIZE];
    size_t length;
};

static UplinkPacket packetQueue[QUEUE_CAPACITY];
static volatile uint8_t queueHead = 0;
static volatile uint8_t queueTail = 0;
static portMUX_TYPE queueMutex = portMUX_INITIALIZER_UNLOCKED;

static bool pushUplinkPacket(const uint8_t* data, size_t len) {
    if (len == 0 || len > MAX_PACKET_SIZE) {
        return false;
    }

    portENTER_CRITICAL(&queueMutex);
    uint8_t nextHead = (queueHead + 1) % QUEUE_CAPACITY;
    if (nextHead == queueTail) {
        portEXIT_CRITICAL(&queueMutex);
        return false; // Queue full
    }

    memcpy(packetQueue[queueHead].data, data, len);
    packetQueue[queueHead].length = len;
    queueHead = nextHead;
    portEXIT_CRITICAL(&queueMutex);
    return true;
}

static bool popUplinkPacket(UplinkPacket* outPacket) {
    portENTER_CRITICAL(&queueMutex);
    if (queueHead == queueTail) {
        portEXIT_CRITICAL(&queueMutex);
        return false; // Queue empty
    }

    *outPacket = packetQueue[queueTail];
    queueTail = (queueTail + 1) % QUEUE_CAPACITY;
    portEXIT_CRITICAL(&queueMutex);
    return true;
}

// LoRa Downlink Interrupt Flag
static volatile bool loraDownlinkReceived = false;
static volatile bool isLoRaTransmitting = false;
static uint8_t loraRxBuffer[MAX_PACKET_SIZE];

// Statistics & Timing
static uint32_t lastLedBlinkMs = 0;
static uint32_t uplinkCount = 0;
static uint32_t downlinkCount = 0;
static float lastRssi = -120.0f;
static float lastSnr = 0.0f;

#if defined(ESP32)
IRAM_ATTR
#endif
static void onDio1Action(void) {
    loraDownlinkReceived = true;
}

static void blinkLed(void) {
    digitalWrite(LED_PIN, LOW); // Active LOW on XIAO
    lastLedBlinkMs = millis();
}

// ─── BLE Server Callbacks ─────────────────────────────────────────────────────
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override {
        isBleConnected = true;
        Serial.printf("[BLE] Phone connected (Peer: %s)\n", NimBLEAddress(desc->peer_ota_addr).toString().c_str());
        digitalWrite(LED_PIN, LOW);
    }

    void onDisconnect(NimBLEServer* pServer) override {
        isBleConnected = false;
        Serial.println(F("[BLE] Phone disconnected, restarting advertising..."));
        digitalWrite(LED_PIN, HIGH);
        NimBLEDevice::startAdvertising();
    }
};

// ─── BLE RX Characteristic Callback (Phone -> ESP32) ─────────────────────────
class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) override {
        std::string rxValue = pChar->getValue();
        if (rxValue.length() > 0) {
            pushUplinkPacket((const uint8_t*)rxValue.data(), rxValue.length());
        }
    }
};

void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // OFF

    Serial.begin(SERIAL_BAUD_RATE);

    uint32_t startMs = millis();
    while (!Serial && (millis() - startMs < 1500)) {
        delay(10);
    }

    Serial.println(F("========================================"));
    Serial.println(F("  RC Car Ground TX (BLE NUS -> LoRa 868)"));
    Serial.println(F("========================================"));

    // ── 1. Initialize SPI & RF Switch Pins ────────────────────────────────────
    Serial.println(F("[LoRa] Initializing SPI bus on GPIO 7 (SCK), 8 (MISO), 9 (MOSI), 41 (NSS)..."));
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

    pinMode(LORA_RF_SWITCH, OUTPUT);
    digitalWrite(LORA_RF_SWITCH, LOW);

    // ── 2. Initialize SX1262 ──────────────────────────────────────────────────
    Serial.println(F("[LoRa] Initializing SX1262 (NSS=41, DIO1=39, NRST=42, BUSY=40, RF_SW=38)..."));

    // Set RF switch pin on SX1262
    radio.setRfSwitchPins(LORA_RF_SWITCH, RADIOLIB_NC);

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
        Serial.printf("[LoRa] TCXO 1.6V initialization failed (%d), retrying with no TCXO...\n", state);
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
        Serial.println(F("[LoRa] SUCCESS! SX1262 ready on 868.0 MHz (SF7, BW500k, 22dBm)"));
    } else {
        Serial.printf("[LoRa] FATAL: Initialization failed with error code %d!\n", state);
        while (true) {
            digitalWrite(LED_PIN, LOW);
            delay(100);
            digitalWrite(LED_PIN, HIGH);
            delay(100);
        }
    }

    // Set up LoRa RX interrupt for downlink telemetry
    radio.setPacketReceivedAction(onDio1Action);
    radio.startReceive();

    // ── 3. Initialize NimBLE Stack & NUS Service ───────────────────────────────
    Serial.println(F("[BLE] Initializing NimBLE NUS Server..."));
    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // +9dBm max TX power

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    NimBLEService* pService = pServer->createService(BLE_NUS_SERVICE_UUID);

    // TX Characteristic (Notify to Phone)
    pTxCharacteristic = pService->createCharacteristic(
        BLE_NUS_TX_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );

    // RX Characteristic (Write from Phone)
    pRxCharacteristic = pService->createCharacteristic(
        BLE_NUS_RX_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pRxCharacteristic->setCallbacks(new RxCallbacks());

    pService->start();

    // Start BLE Advertising
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_NUS_SERVICE_UUID);
    pAdvertising->setName(BLE_DEVICE_NAME);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();

    Serial.printf("[BLE] Advertising as '%s' (NUS Service ready)\n", BLE_DEVICE_NAME);
}

void loop() {
    // ── Turn off LED if not continuously connected ───────────────────────────
    if (!isBleConnected && lastLedBlinkMs > 0 && (millis() - lastLedBlinkMs > 20)) {
        digitalWrite(LED_PIN, HIGH);
        lastLedBlinkMs = 0;
    }

    // ── 1. Uplink: BLE RX Queue -> LoRa TX (Uplink to Rover) ──────────────────
    UplinkPacket packet;
    if (popUplinkPacket(&packet)) {
        isLoRaTransmitting = true;
        int txState = radio.transmit(packet.data, packet.length);
        isLoRaTransmitting = false;

        if (txState == RADIOLIB_ERR_NONE) {
            uplinkCount++;
            blinkLed();
        } else {
            Serial.printf("[LoRa] TX error: %d\n", txState);
        }

        // Re-arm LoRa receiver for telemetry
        radio.startReceive();
    }

    // ── 2. Downlink: LoRa RX -> BLE TX Notification (to Phone) ────────────────
    if (loraDownlinkReceived && !isLoRaTransmitting) {
        loraDownlinkReceived = false;

        size_t len = radio.getPacketLength();
        if (len > 0 && len <= MAX_PACKET_SIZE) {
            int rxState = radio.readData(loraRxBuffer, len);

            if (rxState == RADIOLIB_ERR_NONE) {
                downlinkCount++;
                lastRssi = radio.getRSSI();
                lastSnr = radio.getSNR();

                // Forward telemetry packet to connected Phone over BLE
                if (isBleConnected && pTxCharacteristic != nullptr) {
                    pTxCharacteristic->setValue(loraRxBuffer, len);
                    pTxCharacteristic->notify();
                }
            }
        }

        // Resume listening
        radio.startReceive();
    }
}
