#include "joystick.util.h"

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

struct AnalogValues calculateRightAnalogStickValues(SDL_GameController *controller) {
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

struct AnalogValues calculateLeftAnalogStickValues(SDL_GameController *controller) {
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

float mapStickToDegrees(const int stickValue, const float degreeMin, const float degreeMax, const float step) {
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

int buttonValueToSpeed(SDL_GameController *controller, SDL_GameControllerAxis axis) {
    const int value = SDL_GameControllerGetAxis(controller, axis);

    return (int)roundf((float)value / JOYSTICK_MAX_AXIS_VALUE * 100);
}
