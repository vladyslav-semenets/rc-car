#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __APPLE__
#include "pigpio-mock.h"
#else
#include <pigpio.h>
#endif

#include "libs/env/dotenv.h"
#include "rc-car.h"
#include "udp.h"
#include "serial.h"

static int isRunning = 1;
static RcCar *rcCar = NULL;

static void handleSignal(const int signal) {
    switch (signal) {
        case SIGINT:
        case SIGTERM:
        case SIGTSTP:
            isRunning = 0;
            stopSerial();
            stopUdpServer();
            if (rcCar != NULL) {
                free(rcCar);
                rcCar = NULL;
            }
            gpioWrite(CAR_ESC_ENABLE_PIN, 1);
            gpioTerminate();
            exit(0);
        default:
            break;
    }
}

int main(void) {
    if (gpioInitialise() < 0) {
        fprintf(stderr, "[Main] pigpio initialization failed\n");
        return 1;
    }

    gpioSetMode(CAR_TURNS_SERVO_PIN, PI_OUTPUT);
    gpioSetMode(CAR_ESC_PIN, PI_OUTPUT);
    gpioSetMode(CAR_ESC_SECOND_PIN, PI_OUTPUT);
    gpioSetMode(CAR_ESC_ENABLE_PIN, PI_OUTPUT);
    gpioSetMode(CAR_CAMERA_GIMBAL_PIN1, PI_OUTPUT);
    gpioSetMode(CAR_CAMERA_GIMBAL_PIN3, PI_OUTPUT);
    gpioSetMode(CAR_CAMERA_GIMBAL_PIN4, PI_OUTPUT);

    // Enable ESC power relay on startup
    gpioWrite(CAR_ESC_ENABLE_PIN, 0);

    rcCar = newRcCar();
    env_load(".env", false);

    struct sigaction sa;
    sa.sa_handler = handleSignal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);

    // Initialize Serial Port for LoRa USB Bridge
    const char *serialPort = getenv("SERIAL_PORT");
    if (!serialPort) {
        serialPort = "/dev/ttyACM0";
    }

    const char *baudEnv = getenv("SERIAL_BAUDRATE");
    int baudRate = (baudEnv != NULL) ? atoi(baudEnv) : 115200;

    initSerialDual(serialPort, baudRate, rcCar->processMavlinkCommands, rcCar->processCompactRcPacket);

    // Start UDP Server if port configured
    const char *udpPort = getenv("UDP_SERVER_PORT");
    if (udpPort != NULL) {
        createUdpServer(atoi(udpPort), rcCar->processMavlinkCommands);
    } else {
        printf("[Main] UDP_SERVER_PORT not set, running in Serial-only mode\n");
        while (isRunning) {
            sleep(1);
        }
    }

    return 0;
}
