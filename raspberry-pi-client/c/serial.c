#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <pthread.h>
#include "serial.h"

static int serialFd = -1;
static pthread_t readerThreadHandle;
static volatile bool readerRunning = false;
static pthread_mutex_t writeMutex = PTHREAD_MUTEX_INITIALIZER;
static MavlinkMessageCallback msgCallback = NULL;

static speed_t getSpeed(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
#ifdef B460800
        case 460800: return B460800;
#endif
#ifdef B921600
        case 921600: return B921600;
#endif
        default:     return B115200;
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
    tty.c_cflag &= ~PARENB;        // Clear parity bit
    tty.c_cflag &= ~CSTOPB;        // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;            // 8 bits per byte
    tty.c_cflag &= ~CRTSCTS;       // Disable RTS/CTS hardware flow control
    tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL)

    // Raw input
    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO;          // Disable echo
    tty.c_lflag &= ~ECHOE;         // Disable erasure
    tty.c_lflag &= ~ECHONL;        // Disable new-line echo
    tty.c_lflag &= ~ISIG;          // Disable interpretation of INTR, QUIT and SUSP

    // Disable software flow control & special byte handling
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // Raw output
    tty.c_oflag &= ~OPOST;         // Prevent special interpretation of output bytes
    tty.c_oflag &= ~ONLCR;         // Prevent conversion of newline to CR/LF

    // Blocking read with 100ms timeout
    tty.c_cc[VTIME] = 1;           // Wait for up to 100ms (1 decisecond)
    tty.c_cc[VMIN]  = 0;           // Return as soon as any data is received

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("[Serial] tcsetattr error");
        return -1;
    }

    // Flush any pending bytes
    tcflush(fd, TCIOFLUSH);
    return 0;
}

static void *serialReaderThread(void *arg) {
    (void)arg;
    uint8_t buffer[256];
    mavlink_message_t msg;
    mavlink_status_t status;

    printf("[Serial] Reader thread active\n");

    while (readerRunning) {
        if (serialFd < 0) {
            usleep(100000);
            continue;
        }

        ssize_t bytesRead = read(serialFd, buffer, sizeof(buffer));

        if (bytesRead > 0) {
            for (ssize_t i = 0; i < bytesRead; i++) {
                uint8_t byte = buffer[i];
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

int initSerial(const char *port, int baudRate, MavlinkMessageCallback callback) {
    if (!port) {
        fprintf(stderr, "[Serial] Error: port path is NULL\n");
        return -1;
    }

    msgCallback = callback;

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

    readerRunning = true;
    if (pthread_create(&readerThreadHandle, NULL, serialReaderThread, NULL) != 0) {
        fprintf(stderr, "[Serial] Failed to create reader thread\n");
        close(serialFd);
        serialFd = -1;
        readerRunning = false;
        return -1;
    }

    printf("[Serial] Connected successfully on %s\n", port);
    return 0;
}

void stopSerial(void) {
    readerRunning = false;
    if (serialFd >= 0) {
        pthread_join(readerThreadHandle, NULL);
        close(serialFd);
        serialFd = -1;
        printf("[Serial] Closed connection\n");
    }
}

int sendSerialBinary(const uint8_t *data, uint16_t len) {
    if (serialFd < 0 || !data || len == 0) {
        return -1;
    }

    pthread_mutex_lock(&writeMutex);
    ssize_t written = write(serialFd, data, len);
    pthread_mutex_unlock(&writeMutex);

    if (written < 0) {
        perror("[Serial] write error");
        return -1;
    }
    return (int)written;
}

bool isSerialConnected(void) {
    return (serialFd >= 0 && readerRunning);
}
