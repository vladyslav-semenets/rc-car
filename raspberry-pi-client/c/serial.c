#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "serial.h"

static int serialFd = -1;
static pthread_t readerThreadHandle;
static volatile bool readerRunning = false;
static pthread_mutex_t writeMutex = PTHREAD_MUTEX_INITIALIZER;
static MavlinkMessageCallback msgCallback = NULL;
static CompactRcPacketCallback compactCallback = NULL;

uint8_t computeCrc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if ((crc & 0x80) != 0) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static speed_t getSpeed(int baud) {
    switch (baud) {
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
        case 230400:
            return B230400;
#ifdef B460800
        case 460800:
            return B460800;
#endif
#ifdef B921600
        case 921600:
            return B921600;
#endif
        default:
            return B115200;
    }
}

static int configurePort(int fd, int baud) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("[Serial] tcgetattr error");
        return -1;
    }

    speed_t speed = getSpeed(baud);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    // 8N1 (8 data bits, no parity, 1 stop bit)
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    // Raw input mode
    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO;
    tty.c_lflag &= ~ECHOE;
    tty.c_lflag &= ~ECHONL;
    tty.c_lflag &= ~ISIG;

    // Disable software flow control
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // Raw output mode
    tty.c_oflag &= ~OPOST;
    tty.c_oflag &= ~ONLCR;

    // Non-blocking read with 100ms timeout
    tty.c_cc[VTIME] = 1;
    tty.c_cc[VMIN]  = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("[Serial] tcsetattr error");
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return 0;
}

static void *serialReaderThread(void *arg) {
    (void)arg;
    uint8_t buffer[256];
    uint8_t rcBuffer[RC_COMPACT_LEN];
    int rcIndex = 0;

    mavlink_message_t msg;
    mavlink_status_t status;

    printf("[Serial] Reader thread active (Dual-Mode: Compact 8B + MAVLink)\n");

    while (readerRunning) {
        if (serialFd < 0) {
            usleep(100000);
            continue;
        }

        ssize_t bytesRead = read(serialFd, buffer, sizeof(buffer));

        if (bytesRead > 0) {
            for (ssize_t i = 0; i < bytesRead; i++) {
                uint8_t byte = buffer[i];

                // 1. Compact 8-byte RC Packet Parser (0xAA)
                if (rcIndex == 0) {
                    if (byte == RC_COMPACT_SYNC) {
                        rcBuffer[rcIndex++] = byte;
                    }
                } else {
                    rcBuffer[rcIndex++] = byte;
                    if (rcIndex == RC_COMPACT_LEN) {
                        if (computeCrc8(rcBuffer, RC_COMPACT_LEN - 1) == rcBuffer[RC_COMPACT_LEN - 1]) {
                            if (compactCallback != NULL) {
                                compactCallback((const CompactRcPacket *)rcBuffer);
                            }
                            rcIndex = 0;
                            continue;
                        } else {
                            rcIndex = 0;
                        }
                    }
                }

                // 2. Fallback MAVLink v1/v2 Parser
                if (mavlink_parse_char(MAVLINK_COMM_0, byte, &msg, &status)) {
                    if (msgCallback != NULL) {
                        msgCallback(&msg);
                    }
                }
            }
        } else if (bytesRead < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                perror("[Serial] read error");
                usleep(50000);
            }
        }
    }

    printf("[Serial] Reader thread exiting\n");
    return NULL;
}

int initSerialDual(const char *port, int baudRate, MavlinkMessageCallback mavlinkCb, CompactRcPacketCallback compactCb) {
    if (!port) {
        fprintf(stderr, "[Serial] Error: port path is NULL\n");
        return -1;
    }

    msgCallback = mavlinkCb;
    compactCallback = compactCb;

    printf("[Serial] Opening %s at %d baud...\n", port, baudRate);
    serialFd = open(port, O_RDWR | O_NOCTTY | O_SYNC);

    if (serialFd < 0) {
        fprintf(stderr, "[Serial] Failed to open %s: %s\n", port, strerror(errno));
        return -1;
    }

    if (configurePort(serialFd, baudRate) != 0) {
        close(serialFd);
        serialFd = -1;
        return -1;
    }

    printf("[Serial] Connected successfully on %s\n", port);

    readerRunning = true;
    if (pthread_create(&readerThreadHandle, NULL, serialReaderThread, NULL) != 0) {
        perror("[Serial] Failed to create reader thread");
        close(serialFd);
        serialFd = -1;
        readerRunning = false;
        return -1;
    }

    return 0;
}

int initSerial(const char *port, int baudRate, MavlinkMessageCallback callback) {
    return initSerialDual(port, baudRate, callback, NULL);
}

void stopSerial(void) {
    if (readerRunning) {
        readerRunning = false;
        pthread_join(readerThreadHandle, NULL);
    }

    if (serialFd >= 0) {
        close(serialFd);
        serialFd = -1;
        printf("[Serial] Port closed\n");
    }
}

int sendSerialBinary(const uint8_t *data, uint16_t len) {
    if (serialFd < 0 || !data || len == 0) {
        return -1;
    }

    pthread_mutex_lock(&writeMutex);
    ssize_t totalWritten = 0;
    while (totalWritten < len) {
        ssize_t written = write(serialFd, data + totalWritten, len - totalWritten);
        if (written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                usleep(1000);
                continue;
            }
            perror("[Serial] write error");
            pthread_mutex_unlock(&writeMutex);
            return -1;
        }
        totalWritten += written;
    }
    pthread_mutex_unlock(&writeMutex);
    return (int)totalWritten;
}

bool isSerialConnected(void) {
    return serialFd >= 0 && readerRunning;
}
