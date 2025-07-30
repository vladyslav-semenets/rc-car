#ifndef RC_CAR_H
#define RC_CAR_H
#include <SDL2/SDL.h>
#include "udp.h"

#define JOYSTICK_DEADZONE 3000
#define JOYSTICK_MAX_AXIS_VALUE 32768
#define FIRST_TRANSMISSION_SPEED 1
#define SECOND_TRANSMISSION_SPEED 2
#define THIRD_TRANSMISSION_SPEED 3
#define FOURTH_TRANSMISSION_SPEED 4
#define FIFTH_TRANSMISSION_SPEED 5
#define SIXTH_TRANSMISSION_SPEED 6
#define SEVENTH_TRANSMISSION_SPEED 7
#define EIGHTH_TRANSMISSION_SPEED 8
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

struct CommonActionPayload {
    char *to;
};

struct TurnToActionPayloadData {
    const char *degrees;
};

struct ResetTurnsActionPayloadData {
    char *action;
    const char *degreeOfTurns;
};

struct CameraGimbalSetPitchAngleActionPayloadData {
    char *action;
    const char *degrees;
};

struct ForwardBackwardActionPayloadData {
    const char *carSpeed;
};

struct AnalogValues {
    int x;
    int y;
};

typedef struct {
    struct AnalogValues leftAnalogStickValues;
    struct AnalogValues rightAnalogStickValues;
} JoystickState;

typedef struct RcCar {
    float degreeOfTurns;
    int pitchAngle;
    int speed;
    int transmissionSpeed;
    void (*init)(struct RcCar *self);
    void (*resetCameraGimbal)(struct RcCar *self);
    void (*cameraGimbalTurn)(struct RcCar *self, const float *degrees);
    void (*cameraGimbalSetPitchAngle)(struct RcCar *self);
    void (*changDegreeOfTurns)(struct RcCar *self);
    void (*startCamera)(struct RcCar *self);
    void (*stopCamera)(struct RcCar *self);
    void (*forward)(struct RcCar *self, const int *speed);
    void (*backward)(struct RcCar *self, const int *speed);
    void (*setEscToNeutralPosition)(struct RcCar *self);
    void (*resetTurns)(struct RcCar *self);
    void (*processJoystickEvents)(struct RcCar *self, SDL_Event *e);
    void (*setControllerInstance)(SDL_GameController *controllerInstance);
    void (*setUdpConnection)(UDPConnection *udpConnection);
    void (*onCloseJoystick)();
} RcCar;
RcCar *newRcCar();
#endif
