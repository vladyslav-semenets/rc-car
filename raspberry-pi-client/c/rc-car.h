#ifndef RC_CAR_H
#define RC_CAR_H
#include "libs/mavlink/common/mavlink.h"

#define CAR_TURNS_SERVO_PIN 17
#define CAR_TURNS_MIN_PWM 500
#define CAR_TURNS_MAX_PWM 2500
#define CAR_ESC_PIN 23
#define CAR_ESC_ENABLE_PIN 16
#define CAR_ESC_NEUTRAL_PWM 1500
#define CAR_ESC_MIN_PWM 1000
#define CAR_ESC_MAX_PWM 2500
#define CAR_CAMERA_GIMBAL_PIN1 27
#define CAR_CAMERA_GIMBAL_PIN3 22
#define CAR_CAMERA_GIMBAL_PIN4 24
#define CAR_CAMERA_GIMBAL_MIN_PMW 1000
#define CAR_CAMERA_GIMBAL_MAX_PMW 2000

#define MAVLINK_INIT_COMMAND                            1
#define MAVLINK_CHANGE_DEGREE_OF_TURNS_COMMAND          2
#define MAVLINK_RESET_TURNS_COMMAND                     3
#define MAVLINK_TURN_TO_COMMAND                         4
#define MAVLINK_FORWARD_COMMAND                         5
#define MAVLINK_BACKWARD_COMMAND                        6
#define MAVLINK_SET_ESC_TO_NEUTRAL_POSITION_COMMAND     7
#define MAVLINK_START_CAMERA_COMMAND                    8
#define MAVLINK_STOP_CAMERA_COMMAND                     9
#define MAVLINK_CAMERA_GIMBAL_TURN_TO_COMMAND           10
#define MAVLINK_CAMERA_GIMBAL_SET_PITCH_ANGLE_COMMAND   11
#define MAVLINK_RESET_CAMERA_GIMBAL_COMMAND             12
#define MAVLINK_STEERING_CALIBRATION_ON_COMMAND         13
#define MAVLINK_STEERING_CALIBRATION_OFF_COMMAND        14

#define MPU6050_ADDRESS 0x68
#define GYRO_ZOUT_H 0x47
#define GYRO_SENSITIVITY 131.0
#define MAX_CORRECTION_ANGLE 20.0
#define NEUTRAL_ANGLE 90.0

typedef struct RcCar {
    void (*processWebSocketEvents)(const char *message);
    void (*processMavlinkCommands)(mavlink_message_t *msg);
} RcCar;
RcCar *newRcCar();
#endif
