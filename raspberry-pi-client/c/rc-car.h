#ifndef RC_CAR_H
#define RC_CAR_H

#define CAR_TURNS_SERVO_PIN 17
#define CAR_TURNS_MIN_PWM 500
#define CAR_TURNS_MAX_PWM 2500
#define CAR_ESC_PIN 23
#define CAR_ESC_NEUTRAL_PWM 1500
#define CAR_ESC_MIN_PWM 1000
#define CAR_ESC_MAX_PWM 2000
#define CAR_CAMERA_GIMBAL_PIN1 27
#define CAR_CAMERA_GIMBAL_PIN3 22
#define CAR_CAMERA_GIMBAL_PIN4 24
#define CAR_CAMERA_GIMBAL_MIN_PMW 1000
#define CAR_CAMERA_GIMBAL_MAX_PMW 2000

typedef struct RcCar {
    void (*processWebSocketEvents)(const char *message);
} RcCar;
RcCar *newRcCar();
#endif
