#include <libwebsockets.h>
#include <SDL2/SDL.h>
#include "rc-car.h";
#include "joystick.h"

static SDL_Joystick *joystick = NULL;
static SDL_GameController *controller = NULL;

RcCar *rcCar = NULL;

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

void startJoystickLoop(int *isRunning, struct lws *webSocketInstance) {
    SDL_Event e;

    rcCar = newRcCar(webSocketInstance);

    rcCar->setControllerInstance(controller);

    while (*isRunning) {
        while (SDL_PollEvent(&e)) {
            rcCar->processJoystickEvents(rcCar, &e);

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

    if (rcCar != NULL) {
        rcCar = NULL;
        free(rcCar);
    }

    rcCar->onCloseJoystick();
}
