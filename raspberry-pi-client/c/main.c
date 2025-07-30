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
    gpioSetMode(CAR_ESC_ENABLE_PIN, PI_OUTPUT);
    gpioSetMode(CAR_CAMERA_GIMBAL_PIN1, PI_OUTPUT);
    gpioSetMode(CAR_CAMERA_GIMBAL_PIN3, PI_OUTPUT);
    gpioSetMode(CAR_CAMERA_GIMBAL_PIN4, PI_OUTPUT);

   	gpioWrite(CAR_ESC_ENABLE_PIN, 1);

    rcCar = newRcCar();
    env_load(".env", false);

    struct sigaction sa;

    sa.sa_handler = handleSignal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);

    createUdpServer(atoi(getenv("UDP_SERVER_PORT")), rcCar->processMavlinkCommands);

    return 0;
}
