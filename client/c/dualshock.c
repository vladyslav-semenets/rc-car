#include <libwebsockets.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <math.h>;
#include "stdbool.h"
#include "rc-car.h";
#include "dualshock.h"

#define DEADZONE 8000
#define MAX_AXIS_VALUE 32768.0f

static SDL_Joystick *joystick = NULL;
static SDL_GameController *controller = NULL;

struct AnalogValues {
    int x;
    int y;
};

typedef struct {
    struct AnalogValues leftAnalogStickValues;
} State;


State *state = NULL;

void initializeState() {
    state = malloc(sizeof(State));
    struct AnalogValues leftAnalogStickValues;
    leftAnalogStickValues.x = 0;
    leftAnalogStickValues.y = 0;
    state->leftAnalogStickValues = leftAnalogStickValues;
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
    return abs(values->x) > DEADZONE || abs(values->y) > DEADZONE;
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

float axisXToDegrees(const int axisX) {
    float degrees;

    if (axisX < 0) {
        degrees = 180 - ((float)(axisX + 32768) / 32768.0f) * 90.0f;
        if (degrees < 90) degrees = 90;
    } else {
        degrees = 90 - ((float)(axisX) / 32768.0f) * 90.0f;
        if (degrees < 0) degrees = 0;
    }

    return degrees;
}

int buttonValueToSpeed(SDL_GameControllerAxis axis) {
    const int value = SDL_GameControllerGetAxis(controller, axis);

    return (int)roundf((float)value / MAX_AXIS_VALUE * 100);
}

float getAxisXDegrees() {
    const int axisX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);

    return axisXToDegrees(axisX);
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
                const float degrees = getAxisXDegrees();
                rcCar->turn(rcCar, &degrees);
            } else if (!pressed && previouslyPressed) {
                rcCar->resetTurns(rcCar);
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
        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
            rcCar->degreeOfTurns = rcCar->degreeOfTurns + 0.1f;
            if (rcCar->degreeOfTurns > 180.0f) {
                rcCar->degreeOfTurns = 180.0f;
            }
            rcCar->changDegreeOfTurns(rcCar);
        }

        if (e->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            rcCar->degreeOfTurns = rcCar->degreeOfTurns - 0.1f;
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

void startJoystickLoop(const int *isRunning, struct lws *webSocketInstance) {
    SDL_Event e;
    initializeState();

    RcCar *rcCar = newRcCar(webSocketInstance);

    while (isRunning) {
        while (SDL_PollEvent(&e)) {
            processJoystickEvents(&e, rcCar);

            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_q) {
                break;
            }
        }
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
