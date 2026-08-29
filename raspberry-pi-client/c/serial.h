#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "libs/mavlink/common/mavlink.h"

#define RC_COMPACT_SYNC 0xAA
#define RC_COMPACT_LEN  8

typedef struct __attribute__((packed)) {
    uint8_t sync;        /* 0xAA */
    uint8_t seq;         /* Packet sequence number */
    int8_t  throttle;    /* -100 to +100 (%) */
    uint8_t steering;    /* 0 to 180 (deg, 86 is center) */
    int8_t  gimbalYaw;   /* -90 to +90 (deg) */
    int8_t  gimbalPitch; /* -45 to +45 (deg) */
    uint8_t flags;       /* bit 0: Gyro, bit 1: Unstuck, bit 2: Camera, bit 3..6: Gear */
    uint8_t crc8;        /* CRC-8 */
} CompactRcPacket;

typedef void (*MavlinkMessageCallback)(mavlink_message_t *msg);
typedef void (*CompactRcPacketCallback)(const CompactRcPacket *pkt);

/**
 * Compute CRC-8 (polynomial 0x07).
 */
uint8_t computeCrc8(const uint8_t *data, size_t len);

/**
 * Initialize POSIX serial connection and start dual-mode reader thread.
 *
 * @param port Serial device path (e.g. "/dev/ttyACM0" or "/dev/ttyUSB0")
 * @param baudRate Baud rate integer (e.g. 115200, 921600)
 * @param mavlinkCb Function invoked when a complete MAVLink packet is decoded
 * @param compactCb Function invoked when an 8-byte compact RC packet is decoded
 * @return 0 on success, -1 on failure
 */
int initSerialDual(const char *port, int baudRate, MavlinkMessageCallback mavlinkCb, CompactRcPacketCallback compactCb);
int initSerial(const char *port, int baudRate, MavlinkMessageCallback callback);

/**
 * Stop serial reader thread and close descriptor.
 */
void stopSerial(void);

/**
 * Send binary data to the serial port.
 */
int sendSerialBinary(const uint8_t *data, uint16_t len);

/**
 * Check if the serial connection is currently open and active.
 */
bool isSerialConnected(void);

#endif // SERIAL_H
