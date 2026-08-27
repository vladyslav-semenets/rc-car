#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stdbool.h>
#include "libs/mavlink/common/mavlink.h"

typedef void (*MavlinkMessageCallback)(mavlink_message_t *msg);

/**
 * Initialize POSIX serial connection and start reader thread.
 *
 * @param port Serial device path (e.g. "/dev/ttyACM0" or "/dev/ttyUSB0")
 * @param baudRate Baud rate integer (e.g. 115200, 921600)
 * @param callback Function invoked when a complete MAVLink packet is decoded
 * @return 0 on success, -1 on failure
 */
int initSerial(const char *port, int baudRate, MavlinkMessageCallback callback);

/**
 * Stop serial reader thread and close descriptor.
 */
void stopSerial(void);

/**
 * Send binary MAVLink data to the serial port.
 *
 * @param data Pointer to buffer
 * @param len Length in bytes
 * @return Number of bytes written, or -1 on error
 */
int sendSerialBinary(const uint8_t *data, uint16_t len);

/**
 * Check if the serial connection is currently open and active.
 */
bool isSerialConnected(void);

#endif // SERIAL_H
