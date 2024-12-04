#ifndef JOYSTICK_UTIL_H
#define JOYSTICK_UTIL_H
#include "stdbool.h"
#include "../rc-car.h"
#include <SDL2/SDL.h>
int getLinearConversion(const int value, const int oldMin, const int oldMax, const int newMin, const int newMax);
bool isAnalogStickPressed(struct AnalogValues *values);
struct AnalogValues calculateRightAnalogStickValues(SDL_GameController *controller);
struct AnalogValues calculateLeftAnalogStickValues(SDL_GameController *controller);
float mapStickToDegrees(const int stickValue, const float degreeMin, const float degreeMax, const float step);
int buttonValueToSpeed(SDL_GameController *controller, SDL_GameControllerAxis axis);
#endif //JOYSTICK_UTIL_H
