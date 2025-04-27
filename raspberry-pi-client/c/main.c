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
#define MODE_STR_NUM 4

int isRunning = 1;

RcCar *rcCar = NULL;
pthread_t sendGpsDataThread;
struct gps_data_t gpsData;
static char *mode_str[MODE_STR_NUM] = {
    "n/a",
    "None",
    "2D",
    "3D"
};

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

    if (0 != gps_open("localhost", "2947", &gpsData)) {
        printf("Open error.  Bye, bye\n");
        return 1;
    }

    (void)gps_stream(&gpsData, WATCH_ENABLE | WATCH_JSON, NULL);

    while (gps_waiting(&gpsData, 5000000)) {
        if (-1 == gps_read(&gpsData, NULL, 0)) {
            printf("Read error.  Bye, bye\n");
            break;
        }
        if (MODE_SET != (MODE_SET & gpsData.set)) {
            // did not even get mode, nothing to see here
            continue;
        }
        if (0 > gpsData.fix.mode ||
            MODE_STR_NUM <= gpsData.fix.mode) {
            gpsData.fix.mode = 0;
            }
        printf("Fix mode: %s (%d) Time: ",
               mode_str[gpsData.fix.mode],
               gpsData.fix.mode);
        if (TIME_SET == (TIME_SET & gpsData.set)) {
            // not 32 bit safe
            printf("%ld.%09ld ", gpsData.fix.time.tv_sec,
                   gpsData.fix.time.tv_nsec);
        } else {
            puts("n/a ");
        }
        if (isfinite(gpsData.fix.latitude) &&
            isfinite(gpsData.fix.longitude)) {
            // Display data from the GPS receiver if valid.
            printf("Lat %.6f Lon %.6f\n",
                   gpsData.fix.latitude, gpsData.fix.longitude);
            } else {
                printf("Lat n/a Lon n/a\n");
            }
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
