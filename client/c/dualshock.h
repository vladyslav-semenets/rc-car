#ifndef DUALSHOCK_H
#define DUALSHOCK_H

#include <SDL2/SDL.h>

int initJoystick();
void startJoystickLoop(const int *isRunning, struct lws *webSocketInstance);
void getJoystickAxis(int *x_axis, int *y_axis);
void closeJoystick();

#endif
