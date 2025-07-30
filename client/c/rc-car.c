#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <cjson/cJSON.h>
#include "rc-car.h"
#include "websocket.h"
#include "stdbool.h"
#include "utils//mavlink.util.h"
#include "utils/joystick.util.h"

static SDL_GameController *controller = NULL;
UDPConnection *udpConnectionInstance = NULL;
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
    float params[] = {self->degreeOfTurns};
    sendMavlinkCommand(MAVLINK_CHANGE_DEGREE_OF_TURNS_COMMAND, udpConnectionInstance, params, 1);
}

void resetTurns(RcCar *self) {
    float params[] = {self->degreeOfTurns};
    sendMavlinkCommand(MAVLINK_RESET_TURNS_COMMAND, udpConnectionInstance, params, 1);
}

void turnCar(const float *degrees) {
    if (degrees == NULL) {
        return;
    }
    float params[] = {*degrees};
    sendMavlinkCommand(MAVLINK_TURN_TO_COMMAND, udpConnectionInstance, params, 1);
}

void forward(RcCar *self, const int *speed) {
    int speedValue = prepareSpeedBaseOnSelectedTransmissionSpeed(self, speed);
    if (speed == NULL) {
        return;
    }
    float params[] = {(float) speedValue};
    sendMavlinkCommand(MAVLINK_FORWARD_COMMAND, udpConnectionInstance, params, 1);
}

void backward(const int *speed) {
    if (speed == NULL) {
        return;
    }
    float params[] = {(float) *speed};
    sendMavlinkCommand(MAVLINK_BACKWARD_COMMAND, udpConnectionInstance, params, 1);

}

void setEscToNeutralPosition() {
    float params[] = {};
    sendMavlinkCommand(MAVLINK_SET_ESC_TO_NEUTRAL_POSITION_COMMAND, udpConnectionInstance, params, 0);
}

void startCamera() {
    float params[] = {};
    sendMavlinkCommand(MAVLINK_START_CAMERA_COMMAND, udpConnectionInstance, params, 0);
}

void stopCamera() {
    float params[] = {};
    sendMavlinkCommand(MAVLINK_STOP_CAMERA_COMMAND, udpConnectionInstance, params, 0);
}

void cameraGimbalTurn(const float *degrees) {
    if (degrees == NULL) {
        return;
    }
    float params[] = {*degrees};
    sendMavlinkCommand(MAVLINK_CAMERA_GIMBAL_TURN_TO_COMMAND, udpConnectionInstance, params, 1);
}

void cameraGimbalSetPitchAngle(RcCar *self) {
    float params[] = {(float)self->pitchAngle};
    sendMavlinkCommand(MAVLINK_CAMERA_GIMBAL_SET_PITCH_ANGLE_COMMAND, udpConnectionInstance, params, 1);
}

void resetCameraGimbal() {
    float params[] = {};
    sendMavlinkCommand(MAVLINK_RESET_CAMERA_GIMBAL_COMMAND, udpConnectionInstance, params, 0);
}

void startSteeringCalibration() {
    float params[] = {};
    sendMavlinkCommand(MAVLINK_STEERING_CALIBRATION_ON_COMMAND, udpConnectionInstance, params, 0);
}

void stopSteeringCalibration() {
    float params[] = {};
    sendMavlinkCommand(MAVLINK_STEERING_CALIBRATION_OFF_COMMAND, udpConnectionInstance, params, 0);
}

void init(RcCar *self) {
    float params[] = {(float)self->speed, self->degreeOfTurns};
    sendMavlinkCommand(MAVLINK_INIT_COMMAND, udpConnectionInstance, params, 2);
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
                    degrees = mapStickToDegrees(axisXValue, 0, self->degreeOfTurns, 0.001);
                } else {
                    degrees = mapStickToDegrees(axisXValue, self->degreeOfTurns, 140, 0.001);
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

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_X) {
            const int initialSpeed = 11;
            forward(self, &initialSpeed);
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_B) {
            setEscToNeutralPosition();
        }
    }
}

void setControllerInstance(SDL_GameController *controllerInstance) {
    initializeJoystickState();
    controller = controllerInstance;
}

void setUdpConnectionInstance(UDPConnection *udpConnection) {
    udpConnectionInstance = udpConnection;
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
    rcCar->setUdpConnection = setUdpConnectionInstance;
    rcCar->setControllerInstance = setControllerInstance;
    rcCar->processJoystickEvents = processJoystickEvents;
    rcCar->onCloseJoystick = onCloseJoystick;
    return rcCar;
}
