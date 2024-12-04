#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <cjson/cJSON.h>
#include "rc-car.h"
#include "websocket.h"
#include "stdbool.h"

#define JOYSTICK_DEADZONE 3000
#define JOYSTICK_MAX_AXIS_VALUE 32768

static SDL_GameController *controller = NULL;
struct lws *webSocketInstance = NULL;

struct CommonActionPayload {
    char *to;
};

struct TurnToActionPayloadData {
    const char *degrees;
};

struct ResetTurnsActionPayloadData {
    char *action;
    const char *degreeOfTurns;
};

struct CameraGimbalSetPitchAngleActionPayloadData {
    char *action;
    const char *degrees;
};

struct ForwardBackwardActionPayloadData {
    const char *carSpeed;
};

struct AnalogValues {
    int x;
    int y;
};

typedef struct {
    struct AnalogValues leftAnalogStickValues;
    struct AnalogValues rightAnalogStickValues;
} State;

State *state = NULL;

void initializeState() {
    state = malloc(sizeof(State));
    struct AnalogValues leftAnalogStickValues;
    struct AnalogValues rightAnalogStickValues;
    leftAnalogStickValues.x = 0;
    leftAnalogStickValues.y = 0;
    rightAnalogStickValues.x = 0;
    rightAnalogStickValues.y = 0;
    state->leftAnalogStickValues = leftAnalogStickValues;
    state->rightAnalogStickValues = rightAnalogStickValues;
}

void cleanupState() {
    if (state != NULL) {
        free(state);
        state = NULL;
    }
}

int getLinearConversion(const int value, const int oldMin, const int oldMax, const int newMin, const int newMax) {
    float result = ((float)(value - oldMin) / (oldMax - oldMin)) * (newMax - newMin) + newMin;

    if (newMax < newMin) {
        if (result < newMax) {
            result = newMax;
        } else if (result > newMin) {
            result = newMin;
        }
    } else {
        if (result > newMax) {
            result = newMax;
        } else if (result < newMin) {
            result = newMin;
        }
    }

    return (int)result;
}

bool isAnalogStickPressed(struct AnalogValues *values) {
    return abs(values->x) > JOYSTICK_DEADZONE || abs(values->y) > JOYSTICK_DEADZONE;
}

struct AnalogValues calculateRightAnalogStickValues() {
    struct AnalogValues result;
    result.x = 0;
    result.y = 0;
    const int rawAxisX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
    const int rawAxisY = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY);

    if (rawAxisX != 0) {
        result.x = getLinearConversion(
            abs(rawAxisX),
            JOYSTICK_DEADZONE,
            JOYSTICK_MAX_AXIS_VALUE,
            JOYSTICK_DEADZONE,
            JOYSTICK_MAX_AXIS_VALUE
        );

        if (rawAxisX < 0 && result.x != 0) {
            result.x *= -1;
        }
    }

    if (rawAxisY != 0) {
        result.y = getLinearConversion(
            abs(rawAxisY),
            JOYSTICK_DEADZONE,
            JOYSTICK_MAX_AXIS_VALUE,
            JOYSTICK_DEADZONE,
            JOYSTICK_MAX_AXIS_VALUE
        );

        if (rawAxisY < 0 && result.y != 0) {
            result.y *= -1;
        }
    }

    return result;
}

struct AnalogValues calculateLeftAnalogStickValues() {
    struct AnalogValues result;
    result.x = 0;
    result.y = 0;
    const int rawAxisX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    const int rawAxisY = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);

    if (rawAxisX != 0) {
        result.x = getLinearConversion(
            abs(rawAxisX),
            JOYSTICK_DEADZONE,
            JOYSTICK_MAX_AXIS_VALUE,
            JOYSTICK_DEADZONE,
            JOYSTICK_MAX_AXIS_VALUE
        );

        if (rawAxisX < 0 && result.x != 0) {
            result.x *= -1;
        }
    }

    if (rawAxisY != 0) {
        result.y = getLinearConversion(
            abs(rawAxisY),
            JOYSTICK_DEADZONE,
            JOYSTICK_MAX_AXIS_VALUE,
            JOYSTICK_DEADZONE,
            JOYSTICK_MAX_AXIS_VALUE
        );

        if (rawAxisY < 0 && result.y != 0) {
            result.y *= -1;
        }
    }

    return result;
}

static float mapStickToDegrees(const int stickValue, const float degreeMin, const float degreeMax, const float step) {
    float angle;

    if (stickValue >= 0) {
        angle = degreeMax - (float)stickValue / JOYSTICK_MAX_AXIS_VALUE * (degreeMax - degreeMin);
        angle = roundf(angle / step) * step;
        return fminf(fmaxf(angle, fminf(degreeMin, degreeMax)), fmaxf(degreeMin, degreeMax));
    } else {
        const float clampedValue = fmaxf((float)stickValue, -JOYSTICK_MAX_AXIS_VALUE);
        angle = degreeMax + ((clampedValue + JOYSTICK_MAX_AXIS_VALUE) / JOYSTICK_MAX_AXIS_VALUE) * (degreeMin - degreeMax);
        const float roundedAngle = roundf(angle / step) * step;

        return fmaxf(degreeMin, fminf(roundedAngle, degreeMax));
    }
}

int buttonValueToSpeed(SDL_GameControllerAxis axis) {
    const int value = SDL_GameControllerGetAxis(controller, axis);

    return (int)roundf((float)value / JOYSTICK_MAX_AXIS_VALUE * 100);
}

char* prepareActionPayload(cJSON *data) {
    struct CommonActionPayload actionPayload;
    actionPayload.to = "rc-car-server";

    cJSON *base = cJSON_CreateObject();

    cJSON_AddStringToObject(base, "to", actionPayload.to);
    cJSON_AddItemToObject(base, "data", data);

    char *jsonString = cJSON_Print(base);
    cJSON_Delete(base);

    return jsonString;
}

void changDegreeOfTurns(RcCar *self) {
    int len = snprintf(NULL, 0, "%f", self->degreeOfTurns);
    char *axisXDegreesAsString = malloc(len + 1);
    snprintf(axisXDegreesAsString, len + 1, "%f", self->degreeOfTurns);

    struct ResetTurnsActionPayloadData resetTurnsActionPayloadData;
    resetTurnsActionPayloadData.degreeOfTurns = axisXDegreesAsString;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "change-degree-of-turns");
    cJSON_AddStringToObject(data, "degrees", resetTurnsActionPayloadData.degreeOfTurns);

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, webSocketInstance);
    free(axisXDegreesAsString);
}

void resetTurns(RcCar *self) {
    int len = snprintf(NULL, 0, "%f", self->degreeOfTurns);
    char *axisXDegreesAsString = malloc(len + 1);
    snprintf(axisXDegreesAsString, len + 1, "%f", self->degreeOfTurns);

    struct ResetTurnsActionPayloadData resetTurnsActionPayloadData;
    resetTurnsActionPayloadData.degreeOfTurns = axisXDegreesAsString;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "reset-turns");
    cJSON_AddStringToObject(data, "degrees", resetTurnsActionPayloadData.degreeOfTurns);

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, webSocketInstance);
    free(axisXDegreesAsString);
}

void turnCar(const float *degrees) {
    float degreesValue = *degrees;
    int len = snprintf(NULL, 0, "%f", degreesValue);
    char *axisXDegreesAsString = malloc(len + 1);
    snprintf(axisXDegreesAsString, len + 1, "%f", degreesValue);

    struct TurnToActionPayloadData turnToActionPayload;
    turnToActionPayload.degrees = axisXDegreesAsString;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "turn-to");
    cJSON_AddStringToObject(data, "degrees", turnToActionPayload.degrees);

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, webSocketInstance);
    free(axisXDegreesAsString);
}

void forward(const int *speed) {
    int speedValue = *speed;
    int len = snprintf(NULL, 0, "%d", speedValue);
    char *speedAsString = malloc(len + 1);
    snprintf(speedAsString, len + 1, "%d", speedValue);

    struct ForwardBackwardActionPayloadData forwardBackwardActionPayloadData;
    forwardBackwardActionPayloadData.carSpeed = speedAsString;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "forward");
    cJSON_AddStringToObject(data, "speed", forwardBackwardActionPayloadData.carSpeed);

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, webSocketInstance);
    free(speedAsString);
}

void backward(const int *speed) {
    int speedValue = *speed;
    int len = snprintf(NULL, 0, "%d", speedValue);
    char *speedAsString = malloc(len + 1);
    snprintf(speedAsString, len + 1, "%d", speedValue);

    struct ForwardBackwardActionPayloadData forwardBackwardActionPayloadData;
    forwardBackwardActionPayloadData.carSpeed = speedAsString;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "backward");
    cJSON_AddStringToObject(data, "speed", forwardBackwardActionPayloadData.carSpeed);

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, webSocketInstance);
    free(speedAsString);
}

void setEscToNeutralPosition() {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "set-esc-to-neutral-position");

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, webSocketInstance);
}

void startCamera() {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "start-camera");

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, webSocketInstance);
}

void stopCamera() {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "stop-camera");

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, webSocketInstance);
}

void cameraGimbalTurn(const float *degrees) {
    float degreesValue = *degrees;
    int len = snprintf(NULL, 0, "%f", degreesValue);
    char *axisXDegreesAsString = malloc(len + 1);
    snprintf(axisXDegreesAsString, len + 1, "%f", degreesValue);

    struct TurnToActionPayloadData turnToActionPayload;
    turnToActionPayload.degrees = axisXDegreesAsString;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "camera-gimbal-turn-to");
    cJSON_AddStringToObject(data, "degrees", turnToActionPayload.degrees);

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, webSocketInstance);
    free(axisXDegreesAsString);
}

void cameraGimbalSetPitchAngle(RcCar *self) {
    int len = snprintf(NULL, 0, "%d", self->pitchAngle);
    char *axisXDegreesAsString = malloc(len + 1);
    snprintf(axisXDegreesAsString, len + 1, "%d", self->pitchAngle);

    struct CameraGimbalSetPitchAngleActionPayloadData cameraGimbalSetPitchAngleActionPayloadData;
    cameraGimbalSetPitchAngleActionPayloadData.degrees = axisXDegreesAsString;

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "camera-gimbal-set-pitch-angle");
    cJSON_AddStringToObject(data, "degrees", cameraGimbalSetPitchAngleActionPayloadData.degrees);

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, webSocketInstance);
    free(axisXDegreesAsString);
}

void resetCameraGimbal() {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "reset-camera-gimbal");

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, webSocketInstance);
}

void init(RcCar *self) {
    int lenSpeed = snprintf(NULL, 0, "%d", self->speed);
    char *speedAsString = malloc(lenSpeed + 1);
    snprintf(speedAsString, lenSpeed + 1, "%d", self->speed);

    int lenDegrees = snprintf(NULL, 0, "%f", self->degreeOfTurns);
    char *axisXDegreesAsString = malloc(lenDegrees + 1);
    snprintf(axisXDegreesAsString, lenDegrees + 1, "%f", self->degreeOfTurns);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "init");
    cJSON_AddStringToObject(data, "speed", speedAsString);
    cJSON_AddStringToObject(data, "degrees", axisXDegreesAsString);
    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, webSocketInstance);
    free(speedAsString);
    free(axisXDegreesAsString);
}

void processJoystickEvents(RcCar *self, SDL_Event *e) {
    if (e->type == SDL_CONTROLLERAXISMOTION) {
        if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
            struct AnalogValues cachedLeftAnalogStickValues = state->leftAnalogStickValues;
            struct AnalogValues values = calculateLeftAnalogStickValues();
            bool pressed = isAnalogStickPressed(&values);
            bool previouslyPressed = isAnalogStickPressed(&cachedLeftAnalogStickValues);

            if (!pressed && !previouslyPressed) {
                return;
            }

            state->leftAnalogStickValues = values;

            if (pressed && previouslyPressed) {
                float degrees = 0;
                const int axisXValue = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
                if (axisXValue > 0) {
                    degrees = mapStickToDegrees(axisXValue, 0, self->degreeOfTurns, 0.01);
                } else {
                    degrees = mapStickToDegrees(axisXValue, self->degreeOfTurns, 140, 0.01);
                }

                turnCar(&degrees);
            } else if (!pressed && previouslyPressed) {
                resetTurns(self);
            }
        }

        if (e->caxis.axis == SDL_CONTROLLER_AXIS_RIGHTX) {
            struct AnalogValues cachedRightAnalogStickValues = state->rightAnalogStickValues;
            struct AnalogValues values = calculateRightAnalogStickValues();
            bool pressed = isAnalogStickPressed(&values);
            bool previouslyPressed = isAnalogStickPressed(&cachedRightAnalogStickValues);

            if (!pressed && !previouslyPressed) {
                return;
            }

            state->rightAnalogStickValues = values;

            if (pressed && previouslyPressed) {
                float degrees = 0;
                const int axisXValue = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
                if (axisXValue > 0) {
                    degrees = mapStickToDegrees(axisXValue, 90, 0, 0.01);
                } else {
                    degrees = mapStickToDegrees(axisXValue, 0, 90, 0.01) * -1;
                }
                cameraGimbalTurn(&degrees);
            } else if (!pressed && previouslyPressed) {
                resetCameraGimbal();
            }
        }

        if (e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
            const int value = e->caxis.value;

            if (value > 1000) {
                const int speed = buttonValueToSpeed(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
                forward(&speed);
            }
        }

        if (e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            const int value = e->caxis.value;

            if (value > 1000) {
                const int speed = buttonValueToSpeed(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
                backward(&speed);
            }
        }

        if (
            e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT
            || e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT
        ) {
            const int value = e->caxis.value;

            if (value < 1000) {
                setEscToNeutralPosition();
            }
        }
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            init(self);
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
            self->pitchAngle = self->pitchAngle - 1;
            if (self->pitchAngle < -90) {
                self->pitchAngle = -90;
            }
            cameraGimbalSetPitchAngle(self);
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
            if (self->pitchAngle < 0) {
                self->pitchAngle = (abs(self->pitchAngle) - 1) * -1;
            } else {
                self->pitchAngle = self->pitchAngle + 1;
            }
            if (self->pitchAngle > 90) {
                self->pitchAngle = 90;
            }
            cameraGimbalSetPitchAngle(self);
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
            self->degreeOfTurns = self->degreeOfTurns + 1.0f;
            if (self->degreeOfTurns > 180.0f) {
                self->degreeOfTurns = 180.0f;
            }
            changDegreeOfTurns(self);
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            self->degreeOfTurns = self->degreeOfTurns - 1.0f;
            if (self->degreeOfTurns < 0) {
                self->degreeOfTurns = 0;
            }
            changDegreeOfTurns(self);
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_A) {
            stopCamera();
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_Y) {
            startCamera();
        }
    }
}

void setControllerInstance(SDL_GameController *controllerInstance) {
    initializeState();
    controller = controllerInstance;
}

void setWebSocketInstance(struct lws *instance) {
    webSocketInstance = instance;
}

void onCloseJoystick() {
    cleanupState();
}

RcCar *newRcCar() {
    RcCar *rcCar = (RcCar *)malloc(sizeof(RcCar));
    rcCar->degreeOfTurns = 83.0f;
    rcCar->speed = 50;
    rcCar->pitchAngle = 0;
    rcCar->setWebSocketInstance = setWebSocketInstance;
    rcCar->setControllerInstance = setControllerInstance;
    rcCar->processJoystickEvents = processJoystickEvents;
    rcCar->onCloseJoystick = onCloseJoystick;
    return rcCar;
}
