#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <cjson/cJSON.h>
#include <pigpio.h>
#include "libs/env/dotenv.h"
#include "websocket.h"
#include "rc-car.h"

int isRunning = 1;

RcCar *rcCar = NULL;

typedef enum {
    TURN_TO,
    FORWARD,
    BACKWARD,
    RESET_TURNS,
    CHANGE_DEGREE_OF_TURNS,
    INIT,
    SET_ESC_TO_NEUTRAL_POSITION,
    ACTION_UNKNOWN
} ActionType;

ActionType getActionType(const char *actionStr) {
    if (strcmp(actionStr, "turn-to") == 0) {
        return TURN_TO;
    } else if (strcmp(actionStr, "reset-turns") == 0) {
        return RESET_TURNS;
    } else if (strcmp(actionStr, "change-degree-of-turns") == 0) {
        return CHANGE_DEGREE_OF_TURNS;
    } else if (strcmp(actionStr, "forward") == 0) {
        return FORWARD;
    } else if (strcmp(actionStr, "backward") == 0) {
        return BACKWARD;
    } else if (strcmp(actionStr, "set-esc-to-neutral-position") == 0) {
        return SET_ESC_TO_NEUTRAL_POSITION;
    } else if (strcmp(actionStr, "init") == 0) {
        return INIT;
    } else {
        return ACTION_UNKNOWN;
    }
}

void handleSignal(const int signal) {
    switch (signal) {
        case SIGINT:
        case SIGTERM:
        case SIGTSTP:
            isRunning = 0;
            closeWebSocketServer();
            exit(0);
        default:
            break;
    }
}

void processWebSocketEvents(const char *message) {
    cJSON *json = cJSON_Parse(message);
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    const cJSON *rawAction = cJSON_GetObjectItem(data, "action");
    if (rawAction && cJSON_IsString(rawAction)) {
        ActionType action = getActionType(rawAction->valuestring);
        switch (action) {
            case INIT: {
                const cJSON *rawDegrees = cJSON_GetObjectItem(data, "degrees");
                const float degrees = strtof(rawDegrees->valuestring, NULL);
                rcCar->turnTo(rcCar, &degrees);
                rcCar->setEscToNeutralPosition(rcCar);
            }
            break;
            case CHANGE_DEGREE_OF_TURNS:
            case RESET_TURNS:
            case TURN_TO: {
                const cJSON *rawDegrees = cJSON_GetObjectItem(data, "degrees");
                const float degrees = strtof(rawDegrees->valuestring, NULL);
                rcCar->turnTo(rcCar, &degrees);
            }
            break;
            case FORWARD:
            case BACKWARD: {
                const cJSON *rawDegrees = cJSON_GetObjectItem(data, "speed");
                const int speed = (int) strtof(rawDegrees->valuestring, NULL);
                rcCar->move(rcCar, &speed, rawAction->valuestring);
            }
            break;
            case SET_ESC_TO_NEUTRAL_POSITION: {
                rcCar->setEscToNeutralPosition(rcCar);
            }
            break;

            default:
                break;
        }
    }

    cJSON_Delete(json);
}

int main() {
    if (gpioInitialise() < 0) {
        fprintf(stderr, "pigpio initialization failed\n");
        return 1;
    }

    gpioSetMode(CAR_TURNS_SERVO_PIN, PI_OUTPUT);
    gpioSetMode(CAR_ESC_PIN, PI_OUTPUT);

    rcCar = newRcCar();
    env_load(".env", false);

    struct sigaction sa;
    WebSocketConnection webSocketConnection = connectToWebSocketServer();

    sa.sa_handler = handleSignal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);

    setWebSocketEventCallback(processWebSocketEvents);

    while (isRunning) {
        lws_service(webSocketConnection.context, 100);

        if (!isRunning) {
            break;
        }
    }
    return 0;
}
