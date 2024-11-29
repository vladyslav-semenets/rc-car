#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <pigpio.h>
#include "rc-car.h"
#include "websocket.h"

void turnTo(RcCar *self, const float *degrees) {
    const int pulseWidth = (int)floor(
        CAR_TURNS_MIN_PWM +
        ((*degrees / 180.0f) * (CAR_TURNS_MAX_PWM - CAR_TURNS_MIN_PWM))
    );
    gpioServo(CAR_TURNS_SERVO_PIN, pulseWidth);
}

void move(RcCar *self, const int *speed, const char *direction) {
    int pulseWidth = CAR_ESC_NEUTRAL_MAX_PWM;

    if (strcmp(direction, "forward") == 0) {
        pulseWidth = (int)floorf(
            CAR_ESC_NEUTRAL_PWM +
            ((float)(*speed) / 100.0f) * (CAR_ESC_NEUTRAL_MAX_PWM - CAR_ESC_NEUTRAL_PWM)
        );
    } else if (strcmp(direction, "backward") == 0) {
        pulseWidth = (int)floorf(
            CAR_ESC_NEUTRAL_PWM +
            ((float)(*speed) / 100.0f) * (CAR_ESC_NEUTRAL_PWM - CAR_ESC_NEUTRAL_MIN_PWM)
        );
    }

    gpioServo(CAR_TURNS_SERVO_PIN, pulseWidth);
}

void setEscToNeutralPosition(RcCar *self) {
    gpioServo(CAR_TURNS_SERVO_PIN, CAR_ESC_NEUTRAL_PWM);
}

void startCamera(RcCar *self) {

}

void stopCamera(RcCar *self) {

}

RcCar *newRcCar() {
    RcCar *rcCar = (RcCar *)malloc(sizeof(RcCar));
    rcCar->degreeOfTurns = 87.0f;
    rcCar->turnTo = turnTo;
    rcCar->move = move;
    rcCar->setEscToNeutralPosition = setEscToNeutralPosition;
    rcCar->startCamera = startCamera;
    rcCar->stopCamera = stopCamera;
    return rcCar;
}
