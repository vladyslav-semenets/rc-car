#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pigpio.h>
#include <math.h>
#include <cjson/cJSON.h>
#include "libs/env/dotenv.h"
#include "websocket.h"
#include "rc-car.h"
#include <gps.h>
#include <pthread.h>
#define MODE_STR_NUM 4

int isRunning = 1;

RcCar *rcCar = NULL;
pthread_t sendCarGpsDataThread;
struct gps_data_t gpsData;
static char *mode_str[MODE_STR_NUM] = {
    "n/a",
    "None",
    "2D",
    "3D"
};
struct CommonActionPayload {
    char *to;
};

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
            pthread_cancel(sendCarGpsDataThread);
            gps_stream(&gpsData, WATCH_DISABLE, NULL);
            gps_close(&gpsData);
            exit(0);
        default:
            break;
    }
}

void *sendCarGpsData(void *arg) {
    struct lws *webSocketInstance = (struct lws *)arg;

    if (0 != gps_open("localhost", "2947", &gpsData)) {
        printf("Open error.  Bye, bye\n");
        return 1;
    }

    (void)gps_stream(&gpsData, WATCH_ENABLE | WATCH_JSON, NULL);

    while (gps_waiting(&gpsData, 5000000)) {
        if (-1 == gps_read(&gpsData, NULL, 0)) {
            printf("[GPS] Read error\n");
            break;
        }
        if (MODE_SET != (MODE_SET & gpsData.set)) {
            continue;
        }

        if (gpsData.fix.mode > 0 || MODE_STR_NUM <= gpsData.fix.mode) {
            gpsData.fix.mode = 0;
        }

        printf("[GPS] Fix mode: %s (%d) Time: ", mode_str[gpsData.fix.mode], gpsData.fix.mode);

        if (TIME_SET == (TIME_SET & gpsData.set)) {
            printf("[GPS] %ld.%09ld ", gpsData.fix.time.tv_sec, gpsData.fix.time.tv_nsec);
        } else {
            puts("[GPS] n/a ");
        }

        if (isfinite(gpsData.fix.latitude) && isfinite(gpsData.fix.longitude)) {
            struct CommonActionPayload actionPayload;
            actionPayload.to = "rc-car-client-map";

            cJSON *base = cJSON_CreateObject();

            int latitudeLength = snprintf(NULL, 0, "%f", gpsData.fix.latitude);
            char *latitudeAsString = malloc(latitudeLength + 1);
            snprintf(latitudeAsString, latitudeLength + 1, "%f", gpsData.fix.latitude);

            int longitudeLength = snprintf(NULL, 0, "%f", gpsData.fix.longitude);
            char *longitudeAsString = malloc(longitudeLength + 1);
            snprintf(longitudeAsString, longitudeLength + 1, "%f", gpsData.fix.longitude);

            int speedLength = snprintf(NULL, 0, "%f", gpsData.fix.speed);
            char *speedAsString = malloc(speedLength + 1);
            snprintf(speedAsString, speedLength + 1, "%f", gpsData.fix.speed);

            cJSON_AddStringToObject(base, "to", actionPayload.to);
            cJSON_AddStringToObject(base, "latitude", latitudeAsString);
            cJSON_AddStringToObject(base, "longitude", longitudeAsString);
            cJSON_AddStringToObject(base, "speed", speedAsString);

            char *jsonString = cJSON_Print(base);
            cJSON_Delete(base);

            sendWebSocketEvent(jsonString, webSocketInstance);
            free(latitudeAsString);
            free(longitudeAsString);
            free(speedAsString);
        } else {
            printf("[GPS] Lat n/a Lon n/a \n");
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

    pthread_create(&sendCarGpsDataThread, NULL, sendCarGpsData, webSocketConnection.wsi);

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
