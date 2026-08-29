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
#include "serial.h"

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

/* Runtime motor config — updated via MAVLINK_SET_MOTOR_CONFIG_COMMAND (cmd 15).
   Defaults match the #defines in rc-car.h.                                    */
static volatile int cfg_frontTrimUs      = ESC_FRONT_TRIM_US;
static volatile int cfg_rearTrimUs       = ESC_REAR_TRIM_US;
static volatile int cfg_slewMaxUs        = ESC_SLEW_MAX_US;
static volatile int cfg_dirChangeHoldMs  = ESC_DIR_CHANGE_HOLD_MS;
static volatile int cfg_frontLagSteps    = ESC_FRONT_LAG_STEPS;
static volatile int cfg_reverseBrakeMs   = ESC_REVERSE_BRAKE_MS;
static volatile int cfg_reverseNeutralMs = ESC_REVERSE_NEUTRAL_MS;

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

    int rear_trimmed  = rear_pw + cfg_rearTrimUs;

    /* Apply front trim only when going forward — compensates for the higher
       deadband on the front ESC. Backward trim is intentionally skipped
       because the rear ESC has its own deadband characteristics in reverse. */
    int front_trimmed = front_pw;
    if (front_pw > CAR_ESC_NEUTRAL_PWM)
        front_trimmed = front_pw + cfg_frontTrimUs;   /* forward only */

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

/* Hobbywing "brake then reverse" arming — only needed after forward motion.
   From neutral, reverse works immediately. After forward, the ESC brakes on
   the first backward pulse and needs a neutral gap before it will reverse.  */

static bool reverseArmed = false;  /* true once brake sequence is done */

static void *motorThread(void *arg) {
    printf("[Motor] slew thread started (step=%d us / %d ms, dir-hold=%d ms, front-lag=%d steps)\n",
           cfg_slewMaxUs, ESC_SLEW_INTERVAL_MS, cfg_dirChangeHoldMs, cfg_frontLagSteps);

    lagBufInit();

    while (1) {
        usleep(ESC_SLEW_INTERVAL_MS * 1000);

        pthread_mutex_lock(&motorMutex);
        int target   = targetEscPulseWidth;
        int rearCur  = currentRearEscPulseWidth;
        int frontCur = currentFrontEscPulseWidth;
        pthread_mutex_unlock(&motorMutex);

        if (rearCur == target && frontCur == target) continue;

        /* Reset reverse arm when going forward or neutral */
        if (target >= CAR_ESC_NEUTRAL_PWM) reverseArmed = false;

        /* Detect direction crossing — hold neutral to protect motors.
           Skipped when cfg_dirChangeHoldMs == 0 (disabled mode).           */
        bool crossingNeutral =
            (rearCur > CAR_ESC_NEUTRAL_PWM && target < CAR_ESC_NEUTRAL_PWM) ||
            (rearCur < CAR_ESC_NEUTRAL_PWM && target > CAR_ESC_NEUTRAL_PWM);

        if (crossingNeutral && cfg_dirChangeHoldMs > 0) {
            reverseArmed = false;
            lagBufInit();
            applyEscPulse(CAR_ESC_NEUTRAL_PWM);
            pthread_mutex_lock(&motorMutex);
            currentRearEscPulseWidth  = CAR_ESC_NEUTRAL_PWM;
            currentFrontEscPulseWidth = CAR_ESC_NEUTRAL_PWM;
            pthread_mutex_unlock(&motorMutex);
            printf("[Motor] direction change — neutral hold %d ms\n", cfg_dirChangeHoldMs);
            usleep(cfg_dirChangeHoldMs * 1000);
            continue;
        }

        /* Hobbywing brake-then-reverse.
           Skipped when cfg_reverseBrakeMs == 0 (disabled mode).            */
        if (target < CAR_ESC_NEUTRAL_PWM && !reverseArmed && cfg_reverseBrakeMs > 0) {
            reverseArmed = true;
            printf("[Motor] reverse arm: brake %d ms → neutral %d ms\n",
                   cfg_reverseBrakeMs, cfg_reverseNeutralMs);

            applyEscPulse(target);
            usleep(cfg_reverseBrakeMs * 1000);

            applyEscPulse(CAR_ESC_NEUTRAL_PWM);
            usleep(cfg_reverseNeutralMs * 1000);

            lagBufInit();
            pthread_mutex_lock(&motorMutex);
            currentRearEscPulseWidth  = CAR_ESC_NEUTRAL_PWM;
            currentFrontEscPulseWidth = CAR_ESC_NEUTRAL_PWM;
            pthread_mutex_unlock(&motorMutex);
            continue;
        }
        if (cfg_reverseBrakeMs == 0) reverseArmed = true; /* skip arming when disabled */

        /* Normal slew — rear leads, front follows with lag.
           When stopping (target == CAR_ESC_NEUTRAL_PWM), decelerate 3x faster
           so the car stops promptly without coasting or lagging into obstacles. */
        int maxStep = (target == CAR_ESC_NEUTRAL_PWM) ? (cfg_slewMaxUs * 3) : cfg_slewMaxUs;

        int rearDelta = target - rearCur;
        if (rearDelta >  maxStep) rearDelta =  maxStep;
        if (rearDelta < -maxStep) rearDelta = -maxStep;
        rearCur += rearDelta;

        int frontTarget = (target == CAR_ESC_NEUTRAL_PWM) ? target : lagBufPush(rearCur);
        int frontDelta  = frontTarget - frontCur;
        if (frontDelta >  maxStep) frontDelta =  maxStep;
        if (frontDelta < -maxStep) frontDelta = -maxStep;
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
#define WATCHDOG_TIMEOUT_MS 1500

static volatile time_t  lastHbSec     = 0;
static volatile long    lastHbNsec    = 0;
static volatile bool    watchdogArmed = false;  /* false until first packet from controller arrives */
static volatile bool    watchdogFired = false;  /* true after emergency stop */
static pthread_t        watchdogThreadHandle;
static pthread_mutex_t  watchdogMutex = PTHREAD_MUTEX_INITIALIZER;

static void touchWatchdog() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    pthread_mutex_lock(&watchdogMutex);
    lastHbSec  = ts.tv_sec;
    lastHbNsec = ts.tv_nsec;
    if (!watchdogArmed) {
        watchdogArmed = true;
        printf("[Watchdog] Controller connected — failsafe armed (timeout=%d ms)\n", WATCHDOG_TIMEOUT_MS);
    } else if (watchdogFired) {
        watchdogFired = false;
        printf("[Watchdog] Signal restored — car ready\n");
    }
    pthread_mutex_unlock(&watchdogMutex);
}

static void *watchdogThread(void *arg) {
    printf("[Watchdog] Standby — waiting for initial connection from controller...\n");

    while (1) {
        usleep(50000);  /* check every 50 ms */

        pthread_mutex_lock(&watchdogMutex);
        if (!watchdogArmed) {
            pthread_mutex_unlock(&watchdogMutex);
            continue; /* wait for first packet before arming timeout */
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        long elapsed_ms = (now.tv_sec  - lastHbSec)  * 1000
                        + (now.tv_nsec - lastHbNsec) / 1000000;
        bool alreadyFired = watchdogFired;
        pthread_mutex_unlock(&watchdogMutex);

        if (elapsed_ms > WATCHDOG_TIMEOUT_MS && !alreadyFired) {
            printf("[Watchdog] Signal lost for %ld ms — FAILSAFE EMERGENCY STOP TRIGGERED\n", elapsed_ms);
            /* Immediate neutral — bypasses slew */
            lagBufInit();
            pthread_mutex_lock(&motorMutex);
            targetEscPulseWidth       = CAR_ESC_NEUTRAL_PWM;
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

float gyroYawOffset = 0.0;
float scalingFactor = 0.40;   /* deg of counter-steer per deg/s of yaw rate */
float deadZone = 1.0;         /* ignore small noise (< 1 deg/s) */
int   MPU6050Handle = -1;
float currentSteeringCenter = 86.0;

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

/* Collect `samples` readings to compute the gyro Y-axis bias offset (vertical mounting). */
void calibrateMPU6050(int handle, int samples) {
    printf("[MPU6050] Calibrating Y-axis gyro (vertical heatsink mount)...\n");
    float sumY = 0.0;
    for (int i = 0; i < samples; i++) {
        short gyroY = readMPU6050Data(handle, GYRO_YOUT_H);
        sumY += gyroY / GYRO_SENSITIVITY;
        usleep(10000);
    }
    gyroYawOffset = sumY / samples;
    printf("[MPU6050] Gyro Yaw offset: %.2f deg/s\n", gyroYawOffset);
}

/* Convert steering angle (0–180 deg) to servo pulse width and send. */
void turnTo(const float degrees) {
    const int pulseWidth =
        (int)floorf(CAR_TURNS_MIN_PWM +
                    (degrees / 180.0f) * (CAR_TURNS_MAX_PWM - CAR_TURNS_MIN_PWM));
    gpioServo(CAR_TURNS_SERVO_PIN, pulseWidth);
    printf("[Steering] deg=%.1f pwm=%d us (pin %d)\n", degrees, pulseWidth, CAR_TURNS_SERVO_PIN);
}

/* Background thread: reads gyro Y (vertical mounting) and applies a low-pass steering correction
   to keep the car driving straight (disabled while actively turning). */
void *steeringWheelCorrectionThread(void *arg) {
    int handle = *(int *)arg;

    while (1) {
        if (isCarTurning) {
            usleep(20000);  /* sleep while driver is steering to prevent 100% CPU core usage */
            continue;
        }

        short gyroY = readMPU6050Data(handle, GYRO_YOUT_H);
        float angularVelocityYaw = (gyroY / GYRO_SENSITIVITY) - gyroYawOffset;
        float tempCorrectionAngle = 0.0;

        if (fabs(angularVelocityYaw) > deadZone) {
            tempCorrectionAngle = -angularVelocityYaw * scalingFactor;
        }

        /* Clamp correction to safe range */
        if (tempCorrectionAngle >  MAX_CORRECTION_ANGLE) tempCorrectionAngle =  MAX_CORRECTION_ANGLE;
        if (tempCorrectionAngle < -MAX_CORRECTION_ANGLE) tempCorrectionAngle = -MAX_CORRECTION_ANGLE;

        pthread_mutex_lock(&steeringWheelCorrectionMutex);

        /* Low-pass filter (alpha=0.10) to smooth out gyro noise */
        correctionAngle = previousCorrectionAngle +
                          (tempCorrectionAngle - previousCorrectionAngle) * 0.10f;
        previousCorrectionAngle = correctionAngle;

        float currentServoAngle = currentSteeringCenter + correctionAngle;
        if (currentServoAngle > 140.0f) currentServoAngle = 140.0f;
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

/* Toggle ESC enable pin: enable relay immediately. */
void enableDisableEsc() {
    gpioWrite(CAR_ESC_ENABLE_PIN, 0);
    printf("[ESC] enabled\n");
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

/* ── Unstuck (self-recovery rocking) ─────────────────────────────────────────
   Rocks the car forward/backward with escalating power to free it from
   obstacles. Runs in a background thread so it doesn't block MAVLink.
   Any FORWARD or BACKWARD command cancels it immediately.                   */

static volatile bool    unstuckRunning = false;
static pthread_t        unstuckThreadHandle;

typedef struct { float centerAngle; } UnstuckArgs;

static void *unstuckThread(void *arg) {
    UnstuckArgs *a = (UnstuckArgs *)arg;
    float center = a->centerAngle;
    free(a);

    printf("[Unstuck] starting, center=%.1f deg\n", center);

    /* Center steering first */
    turnTo(center);
    usleep(300000);

    /* 3 cycles with escalating power and duration */
    typedef struct { int speed; int fwdMs; int revMs; int neutralMs; } Cycle;
    const Cycle cycles[] = {
        { 40, 400, 400, 200 },
        { 70, 500, 500, 200 },
        { 100, 700, 700, 250 },
    };

    for (int i = 0; i < 3 && unstuckRunning; i++) {
        const Cycle *c = &cycles[i];
        printf("[Unstuck] cycle %d — speed=%d%%\n", i + 1, c->speed);

        /* Forward */
        int fwdTarget = CAR_ESC_NEUTRAL_PWM +
            (int)((float)c->speed / 100.0f * (CAR_ESC_MAX_PWM - CAR_ESC_NEUTRAL_PWM));
        pthread_mutex_lock(&motorMutex);
        targetEscPulseWidth = fwdTarget;
        pthread_mutex_unlock(&motorMutex);
        usleep(c->fwdMs * 1000);
        if (!unstuckRunning) break;

        /* Neutral gap */
        pthread_mutex_lock(&motorMutex);
        targetEscPulseWidth      = CAR_ESC_NEUTRAL_PWM;
        currentRearEscPulseWidth  = CAR_ESC_NEUTRAL_PWM;
        currentFrontEscPulseWidth = CAR_ESC_NEUTRAL_PWM;
        pthread_mutex_unlock(&motorMutex);
        applyEscPulse(CAR_ESC_NEUTRAL_PWM);
        usleep(c->neutralMs * 1000);
        if (!unstuckRunning) break;

        /* Backward — skip brake arm, go direct */
        int revTarget = CAR_ESC_NEUTRAL_PWM -
            (int)((float)c->speed / 100.0f * (CAR_ESC_NEUTRAL_PWM - CAR_ESC_MIN_PWM));
        reverseArmed = true;  /* skip Hobbywing brake sequence during unstuck */
        pthread_mutex_lock(&motorMutex);
        targetEscPulseWidth = revTarget;
        pthread_mutex_unlock(&motorMutex);
        usleep(c->revMs * 1000);
        if (!unstuckRunning) break;

        /* Neutral gap */
        pthread_mutex_lock(&motorMutex);
        targetEscPulseWidth      = CAR_ESC_NEUTRAL_PWM;
        currentRearEscPulseWidth  = CAR_ESC_NEUTRAL_PWM;
        currentFrontEscPulseWidth = CAR_ESC_NEUTRAL_PWM;
        pthread_mutex_unlock(&motorMutex);
        applyEscPulse(CAR_ESC_NEUTRAL_PWM);
        usleep(c->neutralMs * 1000);
    }

    /* Always end at neutral */
    pthread_mutex_lock(&motorMutex);
    targetEscPulseWidth      = CAR_ESC_NEUTRAL_PWM;
    currentRearEscPulseWidth  = CAR_ESC_NEUTRAL_PWM;
    currentFrontEscPulseWidth = CAR_ESC_NEUTRAL_PWM;
    pthread_mutex_unlock(&motorMutex);
    applyEscPulse(CAR_ESC_NEUTRAL_PWM);

    unstuckRunning = false;
    printf("[Unstuck] done\n");
    return NULL;
}

static void startUnstuck(float centerAngle) {
    if (unstuckRunning) {
        /* Cancel if already running */
        unstuckRunning = false;
        printf("[Unstuck] cancelled\n");
        return;
    }
    unstuckRunning = true;
    UnstuckArgs *args = malloc(sizeof(UnstuckArgs));
    args->centerAngle = centerAngle;
    pthread_create(&unstuckThreadHandle, NULL, unstuckThread, args);
    pthread_detach(unstuckThreadHandle);
}

/* ── MAVLink command dispatcher ──────────────────────────────────────────── */

void processMavlinkCommands(mavlink_message_t *msg) {
    /* Touch watchdog on every received MAVLink frame (heartbeat or command) */
    touchWatchdog();

    /* MAVLink HEARTBEAT (msg_id=0) */
    if (msg->msgid == MAVLINK_MSG_ID_HEARTBEAT) {
        return;
    }

    if (msg->msgid == MAVLINK_MSG_ID_COMMAND_LONG) {
        mavlink_command_long_t cmd;
        mavlink_msg_command_long_decode(msg, &cmd);
        printf("[MAVLink] Received command %d (p1=%.1f, p2=%.1f)\n", cmd.command, cmd.param1, cmd.param2);

        switch (cmd.command) {
            case MAVLINK_INIT_COMMAND:
                currentSteeringCenter = cmd.param2;
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
                currentSteeringCenter = cmd.param1;
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
                turnTo(currentSteeringCenter);   /* center steering before correction starts */
                usleep(500000);
                break;

            case MAVLINK_STEERING_CALIBRATION_OFF_COMMAND:
                deinitMPU6050(MPU6050Handle);
                pthread_cancel(steeringWheelCorrectionThreadHandle);
                break;

            case MAVLINK_UNSTUCK_COMMAND:
                startUnstuck(cmd.param1);
                break;

            case MAVLINK_SET_MOTOR_CONFIG_COMMAND:
                cfg_frontTrimUs      = (int)cmd.param1;
                cfg_rearTrimUs       = (int)cmd.param2;
                cfg_slewMaxUs        = (int)cmd.param3;
                cfg_dirChangeHoldMs  = (int)cmd.param4;
                cfg_frontLagSteps    = (int)cmd.param5;
                cfg_reverseBrakeMs   = (int)cmd.param6;
                cfg_reverseNeutralMs = (int)cmd.param7;
                printf("[Config] frontTrim=%d rearTrim=%d slew=%d dirHold=%d lag=%d brake=%d neutral=%d\n",
                       cfg_frontTrimUs, cfg_rearTrimUs, cfg_slewMaxUs,
                       cfg_dirChangeHoldMs, cfg_frontLagSteps,
                       cfg_reverseBrakeMs, cfg_reverseNeutralMs);
                break;

            case MAVLINK_FORWARD_COMMAND:
                if (unstuckRunning) { unstuckRunning = false; }
                move((int)cmd.param1, "forward");
                break;

            case MAVLINK_BACKWARD_COMMAND:
                if (unstuckRunning) { unstuckRunning = false; }
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

    return rcCar;
}
