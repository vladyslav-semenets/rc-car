#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <cjson/cJSON.h>
#include "rc-car.h"
#include "websocket.h"
#include "stdbool.h"
#include "utils/joystick.util.h"

static SDL_GameController *controller = NULL;
struct lws *webSocketInstance = NULL;
bool isSteeringCalibrationOn = false;

JoystickState *joystickState = NULL;

void initializeJoystickState() {
    joystickState = malloc(sizeof(JoystickState));
    struct AnalogValues leftAnalogStickValues;
    struct AnalogValues rightAnalogStickValues;
    leftAnalogStickValues.x = 0;
    leftAnalogStickValues.y = 0;
    rightAnalogStickValues.x = 0;
    rightAnalogStickValues.y = 0;
    joystickState->leftAnalogStickValues = leftAnalogStickValues;
    joystickState->rightAnalogStickValues = rightAnalogStickValues;
}

void cleanupJoystickState() {
    if (joystickState != NULL) {
        free(joystickState);
        joystickState = NULL;
    }
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

int prepareSpeedBaseOnSelectedTransmissionSpeed(RcCar *self, const int *speed) {
    if (!self || !speed) {
        return 0;
    }

    static const struct {
        int level;
        int maxSpeed;
    } speedLimits[] = {
        { FIRST_TRANSMISSION_SPEED, 20 },
        { SECOND_TRANSMISSION_SPEED, 35 },
        { THIRD_TRANSMISSION_SPEED, 45 },
        { FIFTH_TRANSMISSION_SPEED, 65 },
        { SIXTH_TRANSMISSION_SPEED, 85 },
        { EIGHTH_TRANSMISSION_SPEED, 100 }
    };

    for (size_t i = 0; i < sizeof(speedLimits) / sizeof(speedLimits[0]); i++) {
        if (self->transmissionSpeed == speedLimits[i].level && *speed > speedLimits[i].maxSpeed) {
            return speedLimits[i].maxSpeed;
        }
    }

    return *speed;
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

void forward(RcCar *self, const int *speed) {
    int speedValue = prepareSpeedBaseOnSelectedTransmissionSpeed(self, speed);
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

void startSteeringCalibration() {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "steering-calibration-on");

    const char *payload = prepareActionPayload(data);

    sendWebSocketEvent(payload, webSocketInstance);
}

void stopSteeringCalibration() {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", "steering-calibration-off");

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
            struct AnalogValues cachedLeftAnalogStickValues = joystickState->leftAnalogStickValues;
            struct AnalogValues values = calculateLeftAnalogStickValues(controller);
            bool pressed = isAnalogStickPressed(&values);
            bool previouslyPressed = isAnalogStickPressed(&cachedLeftAnalogStickValues);

            if (!pressed && !previouslyPressed) {
                return;
            }

            joystickState->leftAnalogStickValues = values;

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
            struct AnalogValues cachedRightAnalogStickValues = joystickState->rightAnalogStickValues;
            struct AnalogValues values = calculateRightAnalogStickValues(controller);
            bool pressed = isAnalogStickPressed(&values);
            bool previouslyPressed = isAnalogStickPressed(&cachedRightAnalogStickValues);

            if (!pressed && !previouslyPressed) {
                return;
            }

            joystickState->rightAnalogStickValues = values;

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
                const int speed = buttonValueToSpeed(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
                forward(self, &speed);
            }
        }

        if (e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            const int value = e->caxis.value;

            if (value > 1000) {
                const int speed = buttonValueToSpeed(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
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

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK) {
            if (self->transmissionSpeed >= EIGHTH_TRANSMISSION_SPEED) {
                return;
            }
            self->transmissionSpeed = self->transmissionSpeed + 1;
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSTICK) {
            if (self->transmissionSpeed <= FIRST_TRANSMISSION_SPEED) {
                return;
            }
            self->transmissionSpeed = self->transmissionSpeed - 1;
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            if (isSteeringCalibrationOn) {
                isSteeringCalibrationOn = false;
                stopSteeringCalibration();
                return;
            }

            startSteeringCalibration();
            isSteeringCalibrationOn = true;
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
    initializeJoystickState();
    controller = controllerInstance;
}

void setWebSocketInstance(struct lws *instance) {
    webSocketInstance = instance;
}

void onCloseJoystick() {
    cleanupJoystickState();
}

RcCar *newRcCar() {
    RcCar *rcCar = (RcCar *)malloc(sizeof(RcCar));
    rcCar->degreeOfTurns = 90.0f;
    rcCar->speed = 50;
    rcCar->transmissionSpeed = 1;
    rcCar->pitchAngle = 0;
    rcCar->setWebSocketInstance = setWebSocketInstance;
    rcCar->setControllerInstance = setControllerInstance;
    rcCar->processJoystickEvents = processJoystickEvents;
    rcCar->onCloseJoystick = onCloseJoystick;
    return rcCar;
}
