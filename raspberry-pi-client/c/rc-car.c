#include <math.h>
#include "libs/mavlink/common/mavlink.h"
#ifdef __APPLE__
#include "pigpio-mock.h"  /* stub for local development on macOS */
#else
#include <pigpio.h>       /* real pigpio on Raspberry Pi */
#endif
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "stdbool.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#include "rc-car.h"
#include "websocket.h"

pid_t mediaMtxPid = -1;

/* ── Motor slew-rate thread ──────────────────────────────────────────────────
   The MAVLink receive thread only writes targetEscPulseWidth.
   motorThread runs every ESC_SLEW_INTERVAL_MS ms and smoothly moves
   currentEscPulseWidth toward the target, capped at ESC_SLEW_MAX_US per step.
   On direction change (crossing neutral 1500 µs) it holds neutral for
   ESC_DIR_CHANGE_HOLD_MS to prevent back-EMF damage.                        */

static volatile int    targetEscPulseWidth      = CAR_ESC_NEUTRAL_PWM;
static volatile int    currentRearEscPulseWidth  = CAR_ESC_NEUTRAL_PWM;
static volatile int    currentFrontEscPulseWidth = CAR_ESC_NEUTRAL_PWM;
static pthread_t       motorThreadHandle;
static pthread_mutex_t motorMutex = PTHREAD_MUTEX_INITIALIZER;

/* Ring buffer: rear axle's recent pulse widths, used to delay the front axle.
   Depth = ESC_FRONT_LAG_STEPS → front lags rear by that many 10 ms ticks.   */
#define LAG_BUF_SIZE  16   /* must be >= ESC_FRONT_LAG_STEPS + 1              */
static int  lagBuf[LAG_BUF_SIZE];
static int  lagHead = 0;   /* write index */

static void lagBufInit(void) {
    for (int i = 0; i < LAG_BUF_SIZE; i++) lagBuf[i] = CAR_ESC_NEUTRAL_PWM;
}

/* Push rear's new value and read the delayed front target. */
static int lagBufPush(int rearValue) {
    lagBuf[lagHead % LAG_BUF_SIZE] = rearValue;
    lagHead++;
    /* Read the value from ESC_FRONT_LAG_STEPS ticks ago */
    int readIdx = (lagHead - 1 - ESC_FRONT_LAG_STEPS + LAG_BUF_SIZE * 2) % LAG_BUF_SIZE;
    return lagBuf[readIdx];
}

/* Apply separate pulse widths to front and rear ESCs with trim + hard clamp.
   Trim is applied only when moving (not at neutral) so the front motor does
   not creep at rest despite a non-zero ESC_FRONT_TRIM_US offset.           */
static void applyEscPulses(int rear_pw, int front_pw) {
    if (rear_pw  > CAR_ESC_MAX_PWM) rear_pw  = CAR_ESC_MAX_PWM;
    if (rear_pw  < CAR_ESC_MIN_PWM) rear_pw  = CAR_ESC_MIN_PWM;
    if (front_pw > CAR_ESC_MAX_PWM) front_pw = CAR_ESC_MAX_PWM;
    if (front_pw < CAR_ESC_MIN_PWM) front_pw = CAR_ESC_MIN_PWM;

    int rear_trimmed  = rear_pw + ESC_REAR_TRIM_US;

    /* Apply front trim only while moving — preserve exact neutral at rest */
    int front_trimmed = front_pw;
    if (front_pw > CAR_ESC_NEUTRAL_PWM)
        front_trimmed = front_pw + ESC_FRONT_TRIM_US;   /* forward */
    else if (front_pw < CAR_ESC_NEUTRAL_PWM)
        front_trimmed = front_pw - ESC_FRONT_TRIM_US;   /* backward */

    if (rear_trimmed  > CAR_ESC_MAX_PWM) rear_trimmed  = CAR_ESC_MAX_PWM;
    if (rear_trimmed  < CAR_ESC_MIN_PWM) rear_trimmed  = CAR_ESC_MIN_PWM;
    if (front_trimmed > CAR_ESC_MAX_PWM) front_trimmed = CAR_ESC_MAX_PWM;
    if (front_trimmed < CAR_ESC_MIN_PWM) front_trimmed = CAR_ESC_MIN_PWM;

    gpioServo(CAR_ESC_SECOND_PIN, rear_trimmed);   /* rear axle  — GPIO 25 */
    gpioServo(CAR_ESC_PIN,        front_trimmed);  /* front axle — GPIO 23 */
}

/* Convenience: set both axes to the same value (neutral / emergency stop). */
static void applyEscPulse(int pw) {
    applyEscPulses(pw, pw);
}

/* ── Hobbywing reverse arming ────────────────────────────────────────────────
   Without ESC configuration access, reverse requires a two-step sequence:
     1. Send backward signal for ESC_REVERSE_BRAKE_MS  → ESC brakes
     2. Return to neutral for ESC_REVERSE_NEUTRAL_MS   → ESC resets
     3. Send backward signal again                     → ESC enters reverse
   This state machine runs inside motorThread automatically whenever the
   target crosses from neutral/forward into reverse.                         */
#define ESC_REVERSE_BRAKE_MS    350   /* how long to hold the brake signal (ms) */
#define ESC_REVERSE_NEUTRAL_MS  120   /* neutral gap between brake and reverse   */

typedef enum {
    ESC_STATE_IDLE,          /* at neutral or moving forward */
    ESC_STATE_BRAKE,         /* first backward pulse — ESC braking */
    ESC_STATE_REARM_NEUTRAL, /* brief neutral between brake and reverse */
    ESC_STATE_REVERSE,       /* second backward pulse — ESC actually reversing */
} EscReverseState;

static EscReverseState escReverseState = ESC_STATE_IDLE;

static void *motorThread(void *arg) {
    printf("[Motor] slew thread started (step=%d us / %d ms, dir-hold=%d ms, front-lag=%d steps)\n",
           ESC_SLEW_MAX_US, ESC_SLEW_INTERVAL_MS, ESC_DIR_CHANGE_HOLD_MS, ESC_FRONT_LAG_STEPS);

    lagBufInit();

    while (1) {
        usleep(ESC_SLEW_INTERVAL_MS * 1000);

        pthread_mutex_lock(&motorMutex);
        int target   = targetEscPulseWidth;
        int rearCur  = currentRearEscPulseWidth;
        int frontCur = currentFrontEscPulseWidth;
        pthread_mutex_unlock(&motorMutex);

        /* ── Hobbywing reverse arming state machine ─────────────────────── */
        if (target < CAR_ESC_NEUTRAL_PWM && escReverseState == ESC_STATE_IDLE) {
            /* Entering reverse from neutral/forward — start arming sequence */
            printf("[Motor] reverse arm: brake phase (%d ms)\n", ESC_REVERSE_BRAKE_MS);
            escReverseState = ESC_STATE_BRAKE;

            /* Phase 1: send the backward signal so ESC registers the brake */
            applyEscPulse(target);
            usleep(ESC_REVERSE_BRAKE_MS * 1000);

            /* Phase 2: brief neutral */
            printf("[Motor] reverse arm: neutral gap (%d ms)\n", ESC_REVERSE_NEUTRAL_MS);
            applyEscPulse(CAR_ESC_NEUTRAL_PWM);
            usleep(ESC_REVERSE_NEUTRAL_MS * 1000);

            /* Phase 3: ESC is now armed for reverse */
            printf("[Motor] reverse arm: reverse engaged\n");
            escReverseState = ESC_STATE_REVERSE;

            /* Sync current positions to neutral after the gap */
            lagBufInit();
            pthread_mutex_lock(&motorMutex);
            currentRearEscPulseWidth  = CAR_ESC_NEUTRAL_PWM;
            currentFrontEscPulseWidth = CAR_ESC_NEUTRAL_PWM;
            pthread_mutex_unlock(&motorMutex);
            continue;
        }

        /* Reset reverse state when returning to neutral or going forward */
        if (target >= CAR_ESC_NEUTRAL_PWM) {
            escReverseState = ESC_STATE_IDLE;
        }

        /* ── Direction crossing (forward → backward via neutral hold) ───── */
        bool crossingNeutral =
            (rearCur > CAR_ESC_NEUTRAL_PWM && target < CAR_ESC_NEUTRAL_PWM) ||
            (rearCur < CAR_ESC_NEUTRAL_PWM && target > CAR_ESC_NEUTRAL_PWM);

        if (crossingNeutral) {
            lagBufInit();
            applyEscPulse(CAR_ESC_NEUTRAL_PWM);
            pthread_mutex_lock(&motorMutex);
            currentRearEscPulseWidth  = CAR_ESC_NEUTRAL_PWM;
            currentFrontEscPulseWidth = CAR_ESC_NEUTRAL_PWM;
            pthread_mutex_unlock(&motorMutex);
            printf("[Motor] direction change — holding neutral for %d ms\n",
                   ESC_DIR_CHANGE_HOLD_MS);
            usleep(ESC_DIR_CHANGE_HOLD_MS * 1000);
            escReverseState = ESC_STATE_IDLE;
            continue;
        }

        /* ── Normal slew ─────────────────────────────────────────────────── */
        int rearDelta = target - rearCur;
        if (rearDelta >  ESC_SLEW_MAX_US) rearDelta =  ESC_SLEW_MAX_US;
        if (rearDelta < -ESC_SLEW_MAX_US) rearDelta = -ESC_SLEW_MAX_US;
        rearCur += rearDelta;

        /* Front axle follows rear with a lag */
        int frontTarget = lagBufPush(rearCur);
        int frontDelta  = frontTarget - frontCur;
        if (frontDelta >  ESC_SLEW_MAX_US) frontDelta =  ESC_SLEW_MAX_US;
        if (frontDelta < -ESC_SLEW_MAX_US) frontDelta = -ESC_SLEW_MAX_US;
        frontCur += frontDelta;

        applyEscPulses(rearCur, frontCur);

        pthread_mutex_lock(&motorMutex);
        currentRearEscPulseWidth  = rearCur;
        currentFrontEscPulseWidth = frontCur;
        pthread_mutex_unlock(&motorMutex);
    }
    return NULL;
}

/* ── Watchdog ────────────────────────────────────────────────────────────────
   Waits for MAVLink HEARTBEAT every 100 ms from the client.
   If no heartbeat arrives for WATCHDOG_TIMEOUT_MS → emergency stop (once).
   Resets automatically when heartbeat is restored.                          */
#define WATCHDOG_TIMEOUT_MS 2000

static volatile time_t  lastHbSec     = 0;
static volatile long    lastHbNsec    = 0;
static volatile bool    watchdogFired = false;  /* true after emergency stop */
static pthread_t        watchdogThreadHandle;
static pthread_mutex_t  watchdogMutex = PTHREAD_MUTEX_INITIALIZER;

static void touchWatchdog() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    pthread_mutex_lock(&watchdogMutex);
    lastHbSec  = ts.tv_sec;
    lastHbNsec = ts.tv_nsec;
    if (watchdogFired) {
        watchdogFired = false;
        printf("[Watchdog] heartbeat restored — car ready\n");
    }
    pthread_mutex_unlock(&watchdogMutex);
}

static void *watchdogThread(void *arg) {
    printf("[Watchdog] started, timeout=%d ms\n", WATCHDOG_TIMEOUT_MS);
    touchWatchdog();  /* seed the timer on startup */

    while (1) {
        usleep(100000);  /* check every 100 ms */

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        pthread_mutex_lock(&watchdogMutex);
        long elapsed_ms = (now.tv_sec  - lastHbSec)  * 1000
                        + (now.tv_nsec - lastHbNsec) / 1000000;
        bool alreadyFired = watchdogFired;
        pthread_mutex_unlock(&watchdogMutex);

        if (elapsed_ms > WATCHDOG_TIMEOUT_MS && !alreadyFired) {
            printf("[Watchdog] no heartbeat for %ld ms — emergency stop\n", elapsed_ms);
            /* Immediate neutral — bypasses slew */
            pthread_mutex_lock(&motorMutex);
            targetEscPulseWidth  = CAR_ESC_NEUTRAL_PWM;
            currentRearEscPulseWidth  = CAR_ESC_NEUTRAL_PWM;
            currentFrontEscPulseWidth = CAR_ESC_NEUTRAL_PWM;
            pthread_mutex_unlock(&motorMutex);
            applyEscPulse(CAR_ESC_NEUTRAL_PWM);
            pthread_mutex_lock(&watchdogMutex);
            watchdogFired = true;
            pthread_mutex_unlock(&watchdogMutex);
        }
    }
    return NULL;
}

/* ── MPU6050 gyroscope ───────────────────────────────────────────────────── */

float gyroZOffset = 0.0;
float scalingFactor = 15.0;
float deadZone = 0.5;
int   MPU6050Handle = -1;

bool  isCarTurning = false;
float correctionAngle = 0.0;
float previousCorrectionAngle = 0.0;
pthread_mutex_t steeringWheelCorrectionMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t       steeringWheelCorrectionThreadHandle;

/* Wake the MPU6050 by writing 0 to the power management register. */
void initMPU6050(int handle) {
    i2cWriteByteData(handle, 0x6B, 0x00);
    usleep(100000);
}

void deinitMPU6050(int handle) {
    i2cClose(handle);
}

/* Read a 16-bit big-endian value from two consecutive registers. */
short readMPU6050Data(int handle, int reg) {
    int high = i2cReadByteData(handle, reg);
    int low  = i2cReadByteData(handle, reg + 1);
    return (short)((high << 8) | low);
}

/* Collect `samples` readings to compute the gyro Z-axis bias offset. */
void calibrateMPU6050(int handle, int samples) {
    printf("[MPU6050] Calibrating gyro...\n");
    float sumZ = 0.0;
    for (int i = 0; i < samples; i++) {
        short gyroZ = readMPU6050Data(handle, GYRO_ZOUT_H);
        sumZ += gyroZ / GYRO_SENSITIVITY;
        usleep(10000);
    }
    gyroZOffset = sumZ / samples;
    printf("[MPU6050] Gyro Z offset: %.2f deg/s\n", gyroZOffset);
}

/* Convert steering angle (0–180 deg) to servo pulse width and send. */
void turnTo(const float degrees) {
    const int pulseWidth =
        (int)floorf(CAR_TURNS_MIN_PWM +
                    (degrees / 180.0f) * (CAR_TURNS_MAX_PWM - CAR_TURNS_MIN_PWM));
    gpioServo(CAR_TURNS_SERVO_PIN, pulseWidth);
}

/* Background thread: reads gyro Z and applies a low-pass steering correction
   to keep the car driving straight (disabled while actively turning).       */
void *steeringWheelCorrectionThread(void *arg) {
    int handle = *(int *)arg;

    while (1) {
        if (isCarTurning) {
            continue;  /* skip correction while driver is steering */
        }

        short gyroZ = readMPU6050Data(handle, GYRO_ZOUT_H);
        float angularVelocityZ = (gyroZ / GYRO_SENSITIVITY) - gyroZOffset;
        float tempCorrectionAngle = 0.0;

        if (fabs(angularVelocityZ) > deadZone) {
            tempCorrectionAngle = -angularVelocityZ * scalingFactor;
        }

        /* Clamp correction to safe range */
        if (tempCorrectionAngle >  MAX_CORRECTION_ANGLE) tempCorrectionAngle =  MAX_CORRECTION_ANGLE;
        if (tempCorrectionAngle < -MAX_CORRECTION_ANGLE) tempCorrectionAngle = -MAX_CORRECTION_ANGLE;

        pthread_mutex_lock(&steeringWheelCorrectionMutex);

        /* Low-pass filter (alpha=0.05) to smooth out gyro noise */
        correctionAngle = previousCorrectionAngle +
                          (tempCorrectionAngle - previousCorrectionAngle) * 0.05f;
        previousCorrectionAngle = correctionAngle;

        float currentServoAngle = NEUTRAL_ANGLE + correctionAngle;
        if (currentServoAngle > 180.0f) currentServoAngle = 180.0f;
        if (currentServoAngle <   0.0f) currentServoAngle =   0.0f;

        turnTo(currentServoAngle);
        pthread_mutex_unlock(&steeringWheelCorrectionMutex);
        usleep(20000);  /* 50 Hz update rate */
    }

    return NULL;
}

/* ── ESC control ─────────────────────────────────────────────────────────── */

/* Set a new throttle target. The motorThread handles slew and direction change.
   Forward:  1500 → 2000 µs (Hobbywing standard, 500 µs range)
   Backward: 1500 → 1000 µs                                                 */
void move(const int speed, const char *direction) {
    int target = CAR_ESC_NEUTRAL_PWM;

    if (strcmp(direction, "forward") == 0) {
        target = CAR_ESC_NEUTRAL_PWM +
                 (int)floorf((float)speed / 100.0f *
                             (CAR_ESC_MAX_PWM - CAR_ESC_NEUTRAL_PWM));
    } else if (strcmp(direction, "backward") == 0) {
        target = CAR_ESC_NEUTRAL_PWM -
                 (int)floorf((float)speed / 100.0f *
                             (CAR_ESC_NEUTRAL_PWM - CAR_ESC_MIN_PWM));
    }

    /* Hard clamp before handing off to motorThread */
    if (target > CAR_ESC_MAX_PWM) target = CAR_ESC_MAX_PWM;
    if (target < CAR_ESC_MIN_PWM) target = CAR_ESC_MIN_PWM;

    printf("[ESC] cmd=%s speed=%d target=%d us\n", direction, speed, target);

    pthread_mutex_lock(&motorMutex);
    targetEscPulseWidth = target;
    pthread_mutex_unlock(&motorMutex);
}

/* Immediately set both ESCs to neutral, bypassing the slew ramp. */
void setEscToNeutralPosition() {
    lagBufInit();
    pthread_mutex_lock(&motorMutex);
    targetEscPulseWidth      = CAR_ESC_NEUTRAL_PWM;
    currentRearEscPulseWidth  = CAR_ESC_NEUTRAL_PWM;
    currentFrontEscPulseWidth = CAR_ESC_NEUTRAL_PWM;
    pthread_mutex_unlock(&motorMutex);
    applyEscPulse(CAR_ESC_NEUTRAL_PWM);
    printf("[ESC] neutral\n");
}

/* Toggle ESC enable pin: disable for 5 s then re-enable (used during INIT). */
void enableDisableEsc() {
    gpioWrite(CAR_ESC_ENABLE_PIN, 1);
    printf("[ESC] disabled\n");
    sleep(5);
    gpioWrite(CAR_ESC_ENABLE_PIN, 0);
    printf("[ESC] enabled\n");
    sleep(5);
}

/* ── Camera ──────────────────────────────────────────────────────────────── */

void startCamera() {
    mediaMtxPid = fork();

    if (mediaMtxPid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (mediaMtxPid == 0) {
        /* Child process: exec mediamtx */
        execlp(getenv("MEDIAMTX_BIN_PATH"), "mediamtx",
               getenv("MEDIAMTX_CONFIG_PATH"), NULL);
        perror("execlp");
        exit(EXIT_FAILURE);
    }

    printf("[MediaMTX] started with PID %d\n", mediaMtxPid);
}

void stopCamera() {
    if (kill(mediaMtxPid, SIGTERM) == 0) {
        printf("[MediaMTX] sent SIGTERM to PID %d\n", mediaMtxPid);
        int status;
        waitpid(mediaMtxPid, &status, 0);
        if (WIFEXITED(status))
            printf("[MediaMTX] exited with status %d\n", WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            printf("[MediaMTX] killed by signal %d\n", WTERMSIG(status));
    } else {
        perror("[MediaMTX] failed to terminate process");
    }
}

/* ── Camera gimbal ───────────────────────────────────────────────────────── */

void initCameraGimbal() {
    gpioServo(CAR_CAMERA_GIMBAL_PIN1, CAR_CAMERA_GIMBAL_MAX_PMW);
}

/* Map yaw degrees (-90…+90) to servo pulse width. */
void cameraGimbalSetYaw(const float degrees) {
    const int pulseWidth = (int)floorf(
        ((degrees + 90) / 180.0f) *
        (CAR_CAMERA_GIMBAL_MAX_PMW - CAR_CAMERA_GIMBAL_MIN_PMW) +
        CAR_CAMERA_GIMBAL_MIN_PMW);
    gpioServo(CAR_CAMERA_GIMBAL_PIN4, pulseWidth);
}

/* Map pitch degrees (-90…+90) to servo pulse width. */
void cameraGimbalSetPitch(const float degrees) {
    const int pulseWidth = (int)floorf(
        ((degrees + 90) / 180.0f) *
        (CAR_CAMERA_GIMBAL_MAX_PMW - CAR_CAMERA_GIMBAL_MIN_PMW) +
        CAR_CAMERA_GIMBAL_MIN_PMW);
    gpioServo(CAR_CAMERA_GIMBAL_PIN3, pulseWidth);
}

/* ── MAVLink command dispatcher ──────────────────────────────────────────── */

void processMavlinkCommands(mavlink_message_t *msg) {
    /* MAVLink HEARTBEAT (msg_id=0) — watchdog disabled for now */
    if (msg->msgid == MAVLINK_MSG_ID_HEARTBEAT) {
        // touchWatchdog();
        return;
    }

    if (msg->msgid == MAVLINK_MSG_ID_COMMAND_LONG) {
        mavlink_command_long_t cmd;
        mavlink_msg_command_long_decode(msg, &cmd);

        switch (cmd.command) {
            case MAVLINK_INIT_COMMAND:
                turnTo(cmd.param2);
                enableDisableEsc();
                setEscToNeutralPosition();
                initCameraGimbal();
                break;

            case MAVLINK_CHANGE_DEGREE_OF_TURNS_COMMAND:
            case MAVLINK_TURN_TO_COMMAND:
                isCarTurning = true;
                turnTo(cmd.param1);
                break;

            case MAVLINK_RESET_TURNS_COMMAND:
                isCarTurning = false;
                turnTo(cmd.param1);
                break;

            case MAVLINK_STEERING_CALIBRATION_ON_COMMAND:
                MPU6050Handle = i2cOpen(1, MPU6050_ADDRESS, 0);
                if (MPU6050Handle < 0) {
                    printf("[MPU6050] failed to open I2C connection\n");
                }
                initMPU6050(MPU6050Handle);
                calibrateMPU6050(MPU6050Handle, 100);
                if (pthread_create(&steeringWheelCorrectionThreadHandle, NULL,
                                   steeringWheelCorrectionThread, &MPU6050Handle) != 0) {
                    printf("[MPU6050] failed to create correction thread\n");
                }
                turnTo(90.0f);   /* center steering before correction starts */
                usleep(1000000);
                break;

            case MAVLINK_STEERING_CALIBRATION_OFF_COMMAND:
                deinitMPU6050(MPU6050Handle);
                pthread_cancel(steeringWheelCorrectionThreadHandle);
                break;

            case MAVLINK_FORWARD_COMMAND:
                move((int)cmd.param1, "forward");
                break;

            case MAVLINK_BACKWARD_COMMAND:
                move((int)cmd.param1, "backward");
                break;

            case MAVLINK_SET_ESC_TO_NEUTRAL_POSITION_COMMAND:
                setEscToNeutralPosition();
                break;

            case MAVLINK_START_CAMERA_COMMAND:
                stopCamera();
                break;

            case MAVLINK_STOP_CAMERA_COMMAND:
                startCamera();
                break;

            case MAVLINK_CAMERA_GIMBAL_TURN_TO_COMMAND:
                cameraGimbalSetYaw(cmd.param1);
                break;

            case MAVLINK_CAMERA_GIMBAL_SET_PITCH_ANGLE_COMMAND:
                cameraGimbalSetPitch(cmd.param1);
                break;

            case MAVLINK_RESET_CAMERA_GIMBAL_COMMAND:
                cameraGimbalSetYaw(0);
                break;

            default:
                break;
        }
    }
}

/* ── Initialisation ──────────────────────────────────────────────────────── */

RcCar *newRcCar() {
    RcCar *rcCar = malloc(sizeof(RcCar));
    rcCar->processMavlinkCommands = processMavlinkCommands;

    /* Start the motor slew-rate thread */
    if (pthread_create(&motorThreadHandle, NULL, motorThread, NULL) != 0) {
        fprintf(stderr, "[Motor] failed to create slew thread\n");
    }

    /* Watchdog disabled — enable when heartbeat is wired up on the client
    if (pthread_create(&watchdogThreadHandle, NULL, watchdogThread, NULL) != 0) {
        fprintf(stderr, "[Watchdog] failed to create thread\n");
    } */

    return rcCar;
}
