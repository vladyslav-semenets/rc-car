#ifndef RC_CAR_H
#define RC_CAR_H
typedef struct RcCar {
    struct lws *webSocketInstance;
    float degreeOfTurns;
    int pitchAngle;
    int speed;
    void (*init)(struct RcCar *self);
    void (*turn)(struct RcCar *self, const float *degrees);
    void (*resetCameraGimbal)(struct RcCar *self);
    void (*cameraGimbalTurn)(struct RcCar *self, const float *degrees);
    void (*cameraGimbalSetPitchAngle)(struct RcCar *self, const char *direction);
    void (*changDegreeOfTurns)(struct RcCar *self);
    void (*startCamera)(struct RcCar *self);
    void (*stopCamera)(struct RcCar *self);
    void (*forward)(struct RcCar *self, const int *speed);
    void (*backward)(struct RcCar *self, const int *speed);
    void (*setEscToNeutralPosition)(struct RcCar *self);
    void (*resetTurns)(struct RcCar *self);
} RcCar;
RcCar *newRcCar(struct lws *webSocketInstance);
#endif
