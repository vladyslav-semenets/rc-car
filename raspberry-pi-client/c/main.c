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
            exit(0);
        default:
            break;
    }
}

void *sendGpsData(void *arg) {
    struct lws *webSocketInstance = (struct lws *)arg;

    struct gps_data_t gps_data;

    if (gps_open("localhost", "2947", &gps_data) == 0) {
        gps_stream(&gps_data, WATCH_ENABLE | WATCH_JSON, NULL);

        while (true) {
            if (gps_waiting(&gps_data, 5000000)) {
                if (gps_read(&gps_data, NULL, 0) > 0) {
                    if ((gps_data.fix.status == STATUS_FIX) &&
                        (gps_data.fix.mode >= MODE_2D)) {

                        printf("Speed: %.2f m/s\n", gps_data.fix.speed);
                        printf("Satellites used: %d\n", gps_data.satellites_used);
                        printf("Latitude: %f\n", gps_data.fix.latitude);
                        printf("Longitude: %f\n", gps_data.fix.longitude);
                        } else {
                            printf("No fix yet...\n");
                        }
                }
            }
            usleep(500000);
        }
        gps_stream(&gps_data, WATCH_DISABLE, NULL);
        gps_close(&gps_data);
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
