#ifndef RC_CAR_H
#define RC_CAR_H

#define CAR_TURNS_SERVO_PIN 17
#define CAR_TURNS_MIN_PWM 500
#define CAR_TURNS_MAX_PWM 2500
#define CAR_ESC_PIN 23
#define CAR_ESC_NEUTRAL_PWM 1500
#define CAR_ESC_NEUTRAL_MIN_PWM 1000
#define CAR_ESC_NEUTRAL_MAX_PWM 2000

typedef struct RcCar {
    struct lws *webSocketInstance;
    float degreeOfTurns;
    void (*turnTo)(struct RcCar *self, const float *degrees);
    void (*changDegreeOfTurns)(struct RcCar *self);
    void (*startCamera)(struct RcCar *self);
    void (*stopCamera)(struct RcCar *self);
    void (*move)(struct RcCar *self, const int *speed, const char *direction);
    void (*setEscToNeutralPosition)(struct RcCar *self);
    void (*resetTurns)(struct RcCar *self);
} RcCar;
RcCar *newRcCar();
#endif
