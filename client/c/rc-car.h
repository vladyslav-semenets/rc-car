#ifndef RC_CAR_H
#define RC_CAR_H
typedef struct RcCar {
    struct lws *webSocketInstance;
    void (*turn)(struct RcCar *self, const float *degrees);
} RcCar;
RcCar *newRcCar(struct lws *webSocketInstance);
#endif
