#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <pigpio.h>
#include <cjson/cJSON.h>
#include "rc-car.h"
#include "websocket.h"

void turnTo(RcCar *self, const float *degrees) {
    const int pulseWidth = (int)floor(
        CAR_TURNS_MIN_PWM +
        ((*degrees / 180.0f) * (CAR_TURNS_MAX_PWM - CAR_TURNS_MIN_PWM))
    );
    gpioServo(CAR_TURNS_SERVO_PIN, pulseWidth);
}

void move(RcCar *self, const int *speed, const char *direction) {
    int pulseWidth = CAR_ESC_NEUTRAL_MAX_PWM;

    if (strcmp(direction, "forward") == 0) {
        pulseWidth = (int)floorf(
            CAR_ESC_NEUTRAL_PWM +
            ((float)(*speed) / 100.0f) * (CAR_ESC_NEUTRAL_MAX_PWM - CAR_ESC_NEUTRAL_PWM)
        );
    } else if (strcmp(direction, "backward") == 0) {
        pulseWidth = (int)floorf(
            CAR_ESC_NEUTRAL_PWM +
            ((float)(*speed) / 100.0f) * (CAR_ESC_NEUTRAL_PWM - CAR_ESC_NEUTRAL_MIN_PWM)
        );
    }

    gpioServo(CAR_TURNS_SERVO_PIN, pulseWidth);
}

void setEscToNeutralPosition(RcCar *self) {
    gpioServo(CAR_TURNS_SERVO_PIN, CAR_ESC_NEUTRAL_PWM);
}

void startCamera(RcCar *self) {

}

void stopCamera(RcCar *self) {

}

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

ActionType getActionType(const char *action) {
    if (strcmp(action, "turn-to") == 0) {
        return TURN_TO;
    } else if (strcmp(action, "reset-turns") == 0) {
        return RESET_TURNS;
    } else if (strcmp(action, "change-degree-of-turns") == 0) {
        return CHANGE_DEGREE_OF_TURNS;
    } else if (strcmp(action, "forward") == 0) {
        return FORWARD;
    } else if (strcmp(action, "backward") == 0) {
        return BACKWARD;
    } else if (strcmp(action, "set-esc-to-neutral-position") == 0) {
        return SET_ESC_TO_NEUTRAL_POSITION;
    } else if (strcmp(action, "init") == 0) {
        return INIT;
    } else {
        return ACTION_UNKNOWN;
    }
}

void processWebSocketEvents(RcCar *self, const char *message) {
    cJSON *json = cJSON_Parse(message);
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    const cJSON *rawAction = cJSON_GetObjectItem(data, "action");
    if (rawAction && cJSON_IsString(rawAction)) {
        ActionType action = getActionType(rawAction->valuestring);
        switch (action) {
            case INIT: {
                const cJSON *rawDegrees = cJSON_GetObjectItem(data, "degrees");
                const float degrees = strtof(rawDegrees->valuestring, NULL);
                self->turnTo(rcCar, &degrees);
                self->setEscToNeutralPosition(rcCar);
            }
            break;
            case CHANGE_DEGREE_OF_TURNS:
            case RESET_TURNS:
            case TURN_TO: {
                const cJSON *rawDegrees = cJSON_GetObjectItem(data, "degrees");
                const float degrees = strtof(rawDegrees->valuestring, NULL);
                self->turnTo(rcCar, &degrees);
            }
            break;
            case FORWARD:
            case BACKWARD: {
                const cJSON *rawDegrees = cJSON_GetObjectItem(data, "speed");
                const int speed = (int) strtof(rawDegrees->valuestring, NULL);
                self->move(rcCar, &speed, rawAction->valuestring);
            }
            break;
            case SET_ESC_TO_NEUTRAL_POSITION: {
                self->setEscToNeutralPosition(rcCar);
            }
            break;

            default:
                break;
        }
    }

    cJSON_Delete(json);
}

RcCar *newRcCar() {
    RcCar *rcCar = (RcCar *)malloc(sizeof(RcCar));
    rcCar->degreeOfTurns = 87.0f;
    rcCar->turnTo = turnTo;
    rcCar->move = move;
    rcCar->setEscToNeutralPosition = setEscToNeutralPosition;
    rcCar->startCamera = startCamera;
    rcCar->stopCamera = stopCamera;
    rcCar->processWebSocketEvents = processWebSocketEvents;
    return rcCar;
}
