#include <libwebsockets.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include "stdbool.h"
#include "rc-car.h";
#include "dualshock.h"

#define DEADZONE 1000

static SDL_Joystick *joystick = NULL;
static SDL_GameController *controller = NULL;

float axisXToDegrees(const int axisX) {
    float degrees;

    if (axisX < 0) {
        // Map negative values to degrees in the range [90, 180]
        degrees = 180 - ((float)(axisX + 32768) / 32768.0f) * 90.0f;
        if (degrees < 90) degrees = 90;  // Clamp to 90 if value is too low
    } else {
        // Map positive values to degrees in the range [90, 0]
        degrees = 90 - ((float)(axisX) / 32768.0f) * 90.0f;
        if (degrees < 0) degrees = 0;  // Clamp to 0 if value is too low
    }

    return degrees;
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

    controller = SDL_GameControllerOpen(0); // Open the first controller
    if (!controller) {
        printf("SDL_GameControllerOpen Error: %s\n", SDL_GetError());
        return -1;
    }

    joystick = SDL_JoystickOpen(0); // Open the first joystick
    if (!joystick) {
        printf("SDL_JoystickOpen Error: %s\n", SDL_GetError());
        return -1;
    }

    return 0;
}

void startJoystickLoop(const int *isRunning, struct lws *webSocketInstance) {
    SDL_Event e;

    bool isAxisMoving = false;
    RcCar *rcCar = newRcCar(webSocketInstance);

    while (isRunning) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                printf("Button %d pressed\n", e.cbutton.button);
            } else if (e.type == SDL_CONTROLLERBUTTONUP) {
                printf("Button %d released\n", e.cbutton.button);
            } else if (e.type == SDL_CONTROLLERAXISMOTION) {
                // Detect only left/right movement, ignore up/down by checking axisX
                if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX && (e.caxis.value > DEADZONE || e.caxis.value < -DEADZONE)) {
                    isAxisMoving = true;
                    const float degrees = getAxisXDegrees();
                    rcCar->turn(rcCar, &degrees);
                } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX && !(e.caxis.value > DEADZONE || e.caxis.value < -DEADZONE)) {
                    isAxisMoving = false;
                }

                if (!isAxisMoving && e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX && (e.caxis.value <= DEADZONE && e.caxis.value >= -DEADZONE)) {
                    printf("Axis %d returned to default state\n", e.caxis.axis);
                }
            }

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
}
