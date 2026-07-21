#ifndef RC_CAR_H
#define RC_CAR_H
#include "libs/mavlink/common/mavlink.h"

/* ── Steering servo ────────────────────────────────────────────────────────── */
#define CAR_TURNS_SERVO_PIN 17
#define CAR_TURNS_MIN_PWM   500
#define CAR_TURNS_MAX_PWM   2500

/* ── ESC GPIO pins ─────────────────────────────────────────────────────────── */
#define CAR_ESC_PIN         23   /* front axle ESC */
#define CAR_ESC_SECOND_PIN  25   /* rear axle ESC  */
#define CAR_ESC_ENABLE_PIN  16   /* digital enable line (gpioWrite) */

/* ── ESC PWM range (Hobbywing QuicRun 1625) ────────────────────────────────
   Neutral : 1500 µs
   Forward : 1500 → 2000 µs
   Backward: 1500 → 1000 µs                                                 */
#define CAR_ESC_NEUTRAL_PWM 1500
#define CAR_ESC_MIN_PWM     1000
#define CAR_ESC_MAX_PWM     2000

/* ── Slew rate limiter ──────────────────────────────────────────────────────
   Limits how fast the pulse width can change to protect gears and motors.
   25 µs / 10 ms = 2500 µs/s → full throttle ramp-up takes ~200 ms         */
#define ESC_SLEW_MAX_US        25   /* max pulse width change per step (µs) */
#define ESC_SLEW_INTERVAL_MS   10   /* motor thread step interval (ms)      */
#define ESC_DIR_CHANGE_HOLD_MS 150  /* neutral hold duration on direction change (ms) */

/* ── ESC calibration trim ──────────────────────────────────────────────────
   Per-axle pulse width offset to compensate for ESC calibration differences.
   If the car pulls to one side on a straight line, tune these values.
   Typical range: -10 to +10 µs.                                            */
#define ESC_FRONT_TRIM_US  0
#define ESC_REAR_TRIM_US   0

/* ── Camera gimbal GPIO pins ───────────────────────────────────────────────── */
#define CAR_CAMERA_GIMBAL_PIN1 27
#define CAR_CAMERA_GIMBAL_PIN3 22   /* pitch servo */
#define CAR_CAMERA_GIMBAL_PIN4 24   /* yaw servo   */
#define CAR_CAMERA_GIMBAL_MIN_PMW 1000
#define CAR_CAMERA_GIMBAL_MAX_PMW 2000

/* ── MAVLink custom command IDs ────────────────────────────────────────────── */
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

/* ── MPU6050 gyroscope constants ───────────────────────────────────────────── */
#define MPU6050_ADDRESS      0x68   /* I2C address                          */
#define GYRO_ZOUT_H          0x47   /* Z-axis gyro output register (high)   */
#define GYRO_SENSITIVITY     131.0  /* LSB / (deg/s) at ±250 deg/s range    */
#define MAX_CORRECTION_ANGLE 20.0   /* max steering correction angle (deg)  */
#define NEUTRAL_ANGLE        90.0   /* straight-ahead servo angle (deg)     */

typedef struct RcCar {
    void (*processWebSocketEvents)(const char *message);
    void (*processMavlinkCommands)(mavlink_message_t *msg);
} RcCar;

RcCar *newRcCar();

#endif
