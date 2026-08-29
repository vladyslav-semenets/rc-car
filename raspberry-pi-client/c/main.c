#include <signal.h>
#include <unistd.h>
#ifdef __APPLE__
// Include mock pigpio.h
#include "pigpio-mock.h"
#else
// Use real pigpio
#include <pigpio.h>
#endif
#include <math.h>
#include <cjson/cJSON.h>
#include "libs/env/dotenv.h"
#include "websocket.h"
#include "rc-car.h"
#include "udp.h"
#include "serial.h"
#include <gps.h>
#define MODE_STR_NUM 4

int isRunning = 1;

RcCar *rcCar = NULL;

void handleSignal(const int signal) {
    switch (signal) {
        case SIGINT:
        case SIGTERM:
        case SIGTSTP:
            isRunning = 0;
            stopSerial();
            stopUdpServer();
            free(rcCar);
            gpioWrite(CAR_ESC_ENABLE_PIN, 1);
            gpioTerminate();
            exit(0);
        default:
            break;
    }
}

int main() {
    if (gpioInitialise() < 0) {
        fprintf(stderr, "pigpio initialization failed\n");
        return 1;
    }

    gpioSetMode(CAR_TURNS_SERVO_PIN, PI_OUTPUT);
    gpioSetMode(CAR_ESC_PIN, PI_OUTPUT);
    gpioSetMode(CAR_ESC_SECOND_PIN, PI_OUTPUT);
    gpioSetMode(CAR_ESC_ENABLE_PIN, PI_OUTPUT);
    gpioSetMode(CAR_CAMERA_GIMBAL_PIN1, PI_OUTPUT);
    gpioSetMode(CAR_CAMERA_GIMBAL_PIN3, PI_OUTPUT);
    gpioSetMode(CAR_CAMERA_GIMBAL_PIN4, PI_OUTPUT);

   	gpioWrite(CAR_ESC_ENABLE_PIN, 0); // 0 = Active/Enabled

    rcCar = newRcCar();
    env_load(".env", false);

    struct sigaction sa;

    sa.sa_handler = handleSignal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);

    // Initialize Serial Port for LoRa USB Bridge if configured or available
    const char *serialPort = getenv("SERIAL_PORT");
    if (!serialPort) {
        serialPort = "/dev/ttyACM0";
    }
    const char *baudEnv = getenv("SERIAL_BAUDRATE");
    int baudRate = baudEnv ? atoi(baudEnv) : 115200;

    initSerialDual(serialPort, baudRate, rcCar->processMavlinkCommands, rcCar->processCompactRcPacket);

    // Start UDP Server if port configured
    const char *udpPort = getenv("UDP_SERVER_PORT");
    if (udpPort) {
        createUdpServer(atoi(udpPort), rcCar->processMavlinkCommands);
    } else {
        printf("[Main] UDP_SERVER_PORT not set, running in Serial-only mode\n");
        while (isRunning) {
            sleep(1);
        }
    }

    return 0;
}
