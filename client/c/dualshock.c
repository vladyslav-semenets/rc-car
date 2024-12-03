#include <libwebsockets.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <math.h>;
#include "stdbool.h"
#include "rc-car.h";
#include "dualshock.h"

#define DEADZONE 3000
#define MAX_AXIS_VALUE 32768.0f

static SDL_Joystick *joystick = NULL;
static SDL_GameController *controller = NULL;

struct AnalogValues {
    int x;
    int y;
};

typedef struct {
    struct AnalogValues leftAnalogStickValues;
    struct AnalogValues rightAnalogStickValues;
} State;

RcCar *rcCar = NULL;
State *state = NULL;

void initializeState() {
    state = malloc(sizeof(State));
    struct AnalogValues leftAnalogStickValues;
    leftAnalogStickValues.x = 0;
    leftAnalogStickValues.y = 0;
    struct AnalogValues rightAnalogStickValues;
    rightAnalogStickValues.x = 0;
    rightAnalogStickValues.y = 0;
    state->rightAnalogStickValues = rightAnalogStickValues;
}

void cleanupState() {
    if (state != NULL) {
        free(state);
        state = NULL;
    }

    if (rcCar != NULL) {
        free(rcCar);
        rcCar = NULL;
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
    return abs(values->x) > DEADZONE || abs(values->y) > DEADZONE;
}

struct AnalogValues calculateRightAnalogStickValues() {
    struct AnalogValues result;
    result.x = 0;
    result.y = 0;
    const int rawAxisX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
    const int rawAxisY = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY);

    if (rawAxisX != 0) {
        result.x = getLinearConversion(abs(rawAxisX), DEADZONE, 32768, DEADZONE, 32768);

        if (rawAxisX < 0 && result.x != 0) {
            result.x *= -1;
        }
    }

    if (rawAxisY != 0) {
        result.y = getLinearConversion(abs(rawAxisY), DEADZONE, 32768, DEADZONE, 32768);

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
        result.x = getLinearConversion(abs(rawAxisX), DEADZONE, 32768, DEADZONE, 32768);

        if (rawAxisX < 0 && result.x != 0) {
            result.x *= -1;
        }
    }

    if (rawAxisY != 0) {
        result.y = getLinearConversion(abs(rawAxisY), DEADZONE, 32768, DEADZONE, 32768);

        if (rawAxisY < 0 && result.y != 0) {
            result.y *= -1;
        }
    }

    return result;
}

static float mapStickToDegrees(const int stickValue, const float degreeMin, const float degreeMax, const float step) {
    float angle;

    if (stickValue >= 0) {
        angle = degreeMax - (float)stickValue / 32768 * (degreeMax - degreeMin);
        angle = roundf(angle / step) * step;
        return fminf(fmaxf(angle, fminf(degreeMin, degreeMax)), fmaxf(degreeMin, degreeMax));
    } else {
        const float clampedValue = fmaxf((float)stickValue, -32768);
        angle = degreeMax + ((clampedValue + 32768) / 32768) * (degreeMin - degreeMax);
        const float roundedAngle = roundf(angle / step) * step;

        return fmaxf(degreeMin, fminf(roundedAngle, degreeMax));
    }
}

int buttonValueToSpeed(SDL_GameControllerAxis axis) {
    const int value = SDL_GameControllerGetAxis(controller, axis);

    return (int)roundf((float)value / MAX_AXIS_VALUE * 100);
}

int initJoystick() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return -1;
    }

    if (SDL_NumJoysticks() < 1) {
        printf("No dualshock connected\n");
        return -1;
    }

    controller = SDL_GameControllerOpen(0);
    if (!controller) {
        printf("SDL_GameControllerOpen Error: %s\n", SDL_GetError());
        return -1;
    }

    joystick = SDL_JoystickOpen(0);
    if (!joystick) {
        printf("SDL_JoystickOpen Error: %s\n", SDL_GetError());
        return -1;
    }

    return 0;
}

void processJoystickEvents(SDL_Event *e, RcCar *rcCar) {
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
                    degrees = mapStickToDegrees(axisXValue, 0, rcCar->degreeOfTurns, 0.01);
                } else {
                    degrees = mapStickToDegrees(axisXValue, rcCar->degreeOfTurns, 140, 0.01);
                }

                rcCar->turn(rcCar, &degrees);
            } else if (!pressed && previouslyPressed) {
                rcCar->resetTurns(rcCar);
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
                rcCar->cameraGimbalTurn(rcCar, &degrees);
            } else if (!pressed && previouslyPressed) {
                rcCar->resetCameraGimbal(rcCar);
            }
        }

        if (e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
            const int value = e->caxis.value;

            if (value > 1000) {
                const int speed = buttonValueToSpeed(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
                rcCar->forward(rcCar, &speed);
            }
        }

        if (e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            const int value = e->caxis.value;

            if (value > 1000) {
                const int speed = buttonValueToSpeed(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
                rcCar->backward(rcCar, &speed);
            }
        }

        if (
            e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT
            || e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT
        ) {
            const int value = e->caxis.value;

            if (value < 1000) {
                rcCar->setEscToNeutralPosition(rcCar);
            }
        }
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            rcCar->init(rcCar);
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
            rcCar->pitchAngle = rcCar->pitchAngle - 1;
            if (rcCar->pitchAngle < -90) {
                rcCar->pitchAngle = -90;
            }
            rcCar->cameraGimbalSetPitchAngle(rcCar, "bottom");
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
            if (rcCar->pitchAngle < 0) {
                rcCar->pitchAngle = (abs(rcCar->pitchAngle) - 1) * -1;
            } else {
                rcCar->pitchAngle = rcCar->pitchAngle + 1;
            }
            if (rcCar->pitchAngle > 90) {
                rcCar->pitchAngle = 90;
            }
            rcCar->cameraGimbalSetPitchAngle(rcCar, "top");
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
            rcCar->degreeOfTurns = rcCar->degreeOfTurns + 1.0f;
            if (rcCar->degreeOfTurns > 180.0f) {
                rcCar->degreeOfTurns = 180.0f;
            }
            rcCar->changDegreeOfTurns(rcCar);
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            rcCar->degreeOfTurns = rcCar->degreeOfTurns - 1.0f;
            if (rcCar->degreeOfTurns < 0) {
                rcCar->degreeOfTurns = 0;
            }
            rcCar->changDegreeOfTurns(rcCar);
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_A) {
            rcCar->stopCamera(rcCar);
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_Y) {
            rcCar->startCamera(rcCar);
        }
    }
}

void startJoystickLoop(int *isRunning, struct lws *webSocketInstance) {
    SDL_Event e;
    initializeState();

    rcCar = newRcCar(webSocketInstance);

    while (*isRunning) {
        while (SDL_PollEvent(&e)) {
            processJoystickEvents(&e, rcCar);

            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_q) {
                break;
            }
        }

        SDL_Delay(100);
    }
}

void closeJoystick() {
    if (controller) {
        SDL_GameControllerClose(controller);
        controller = NULL;
    }
    if (joystick) {
        SDL_JoystickClose(joystick);
        joystick = NULL;
    }
    SDL_Quit();
    cleanupState();
}
