#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pigpio.h>
#include "libs/env/dotenv.h"
#include "websocket.h"
#include "rc-car.h"
#include <gps.h>
#include <pthread.h>

int isRunning = 1;

RcCar *rcCar = NULL;
pthread_t sendGpsDataThread;
struct gps_data_t gpsData;

// Signal handling for clean shutdown
void handleSignal(const int signal) {
    switch (signal) {
        case SIGINT:
        case SIGTERM:
        case SIGTSTP:
            isRunning = 0;
            closeWebSocketServer();
            free(rcCar);
            gpioWrite(CAR_ESC_ENABLE_PIN, 1);
            gpioTerminate();
            pthread_cancel(sendGpsDataThread);
            gps_stream(&gpsData, WATCH_DISABLE, NULL);
            gps_close(&gpsData);
            exit(0);
        default:
            break;
    }
}

void *sendGpsData(void *arg) {
    struct lws *webSocketInstance = (struct lws *)arg;

    if (gps_open("localhost", "2947", &gpsData) == 0) {
        gps_stream(&gpsData, WATCH_ENABLE | WATCH_JSON, NULL);

        while (isRunning) {
            if (gps_waiting(&gpsData, 5000000)) {
                if (gps_read(&gpsData, NULL, 0) > 0) {
                    if (gpsData.fix.status == STATUS_FIX && gpsData.fix.mode >= MODE_2D) {
                        printf("Speed: %.2f m/s\n", gpsData.fix.speed);
                        printf("Satellites used: %d\n", gpsData.satellites_used);
                        printf("Latitude: %f\n", gpsData.fix.latitude);
                        printf("Longitude: %f\n", gpsData.fix.longitude);
                        } else {
                            printf("No fix yet... %s \n", gpsData.fix.status);
                        }
                }
            }
            usleep(500000);
        }
    } else {
        printf("Failed to open GPS.\n");
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
    WebSocketConnection webSocketConnection = connectToWebSocketServer();

    pthread_create(&sendGpsDataThread, NULL, sendGpsData, webSocketConnection.wsi);

    sa.sa_handler = handleSignal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);

    setWebSocketEventCallback(rcCar->processWebSocketEvents);

    while (isRunning) {
        lws_service(webSocketConnection.context, 100);
    }

    return 0;
}
