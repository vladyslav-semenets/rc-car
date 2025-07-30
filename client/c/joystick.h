#ifndef JOYSTICK_H
#define JOYSTICK_H
#include <SDL2/SDL.h>
#include "udp.h"
typedef void (*ProcessJoystickEventsCallback)(SDL_Event *e);
int initJoystick();
void startJoystickLoop(int *isRunning, UDPConnection *udpConnection);
void getJoystickAxis(int *x_axis, int *y_axis);
void closeJoystick();
#endif
