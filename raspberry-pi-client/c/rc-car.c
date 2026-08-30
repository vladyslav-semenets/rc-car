#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "libs/mavlink/common/mavlink.h"

#ifdef __APPLE__
#include "pigpio-mock.h"
#else
#include <pigpio.h>
#endif

#include "rc-car.h"
#include "serial.h"

pid_t mediaMtxPid = -1;

/* ── Motor slew-rate thread ──────────────────────────────────────────────────
   Smoothly slews ESC pulse width toward targetEscPulseWidth to protect gears.
   On direction change (crossing neutral 1500 µs), holds neutral for
   ESC_DIR_CHANGE_HOLD_MS to avoid back-EMF spikes.                          */

static volatile int targetEscPulseWidth       = CAR_ESC_NEUTRAL_PWM;
static volatile int currentRearEscPulseWidth  = CAR_ESC_NEUTRAL_PWM;
static volatile int currentFrontEscPulseWidth = CAR_ESC_NEUTRAL_PWM;
static pthread_t motorThreadHandle;
static pthread_mutex_t motorMutex = PTHREAD_MUTEX_INITIALIZER;

/* Runtime motor config — updated via MAVLINK_SET_MOTOR_CONFIG_COMMAND (cmd 15) */
static volatile int cfg_frontTrimUs      = ESC_FRONT_TRIM_US;
static volatile int cfg_rearTrimUs       = ESC_REAR_TRIM_US;
static volatile int cfg_slewMaxUs        = ESC_SLEW_MAX_US;
static volatile int cfg_dirChangeHoldMs  = ESC_DIR_CHANGE_HOLD_MS;
static volatile int cfg_frontLagSteps    = ESC_FRONT_LAG_STEPS;
static volatile int cfg_reverseBrakeMs   = ESC_REVERSE_BRAKE_MS;
static volatile int cfg_reverseNeutralMs = ESC_REVERSE_NEUTRAL_MS;

/* Ring buffer: rear axle's recent pulse widths, used to delay front axle */
#define LAG_BUF_SIZE 16
static int lagBuf[LAG_BUF_SIZE];
static int lagHead = 0;

static void lagBufInit(void) {
    for (int i = 0; i < LAG_BUF_SIZE; i++) {
        lagBuf[i] = CAR_ESC_NEUTRAL_PWM;
    }
}

static int lagBufPush(int rearValue) {
    lagBuf[lagHead % LAG_BUF_SIZE] = rearValue;
    lagHead++;
    int readIdx = (lagHead - 1 - ESC_FRONT_LAG_STEPS + LAG_BUF_SIZE * 2) % LAG_BUF_SIZE;
    return lagBuf[readIdx];
}

static void applyEscPulses(int rear_pw, int front_pw) {
    if (rear_pw > CAR_ESC_MAX_PWM) {
        rear_pw = CAR_ESC_MAX_PWM;
    }
    if (rear_pw < CAR_ESC_MIN_PWM) {
        rear_pw = CAR_ESC_MIN_PWM;
    }
    if (front_pw > CAR_ESC_MAX_PWM) {
        front_pw = CAR_ESC_MAX_PWM;
    }
    if (front_pw < CAR_ESC_MIN_PWM) {
        front_pw = CAR_ESC_MIN_PWM;
    }

    int rear_trimmed = rear_pw + cfg_rearTrimUs;
    int front_trimmed = front_pw;

    if (front_pw > CAR_ESC_NEUTRAL_PWM) {
        front_trimmed = front_pw + cfg_frontTrimUs;
    }

    if (rear_trimmed > CAR_ESC_MAX_PWM) {
        rear_trimmed = CAR_ESC_MAX_PWM;
    }
    if (rear_trimmed < CAR_ESC_MIN_PWM) {
        rear_trimmed = CAR_ESC_MIN_PWM;
    }
    if (front_trimmed > CAR_ESC_MAX_PWM) {
        front_trimmed = CAR_ESC_MAX_PWM;
    }
    if (front_trimmed < CAR_ESC_MIN_PWM) {
        front_trimmed = CAR_ESC_MIN_PWM;
    }

    gpioServo(CAR_ESC_SECOND_PIN, rear_trimmed);
    gpioServo(CAR_ESC_PIN, front_trimmed);
}

static void applyEscPulse(int pw) {
    applyEscPulses(pw, pw);
}

static bool reverseArmed = false;

static void *motorThread(void *arg) {
    (void)arg;
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

        if (rearCur == target && frontCur == target) {
            continue;
        }

        if (target >= CAR_ESC_NEUTRAL_PWM) {
            reverseArmed = false;
        }

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

        if (target < CAR_ESC_NEUTRAL_PWM && !reverseArmed && cfg_reverseBrakeMs > 0) {
            reverseArmed = true;
            printf("[Motor] reverse arm: brake %d ms -> neutral %d ms\n",
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

        if (cfg_reverseBrakeMs == 0) {
            reverseArmed = true;
        }

        int maxStep = (target == CAR_ESC_NEUTRAL_PWM) ? (cfg_slewMaxUs * 3) : cfg_slewMaxUs;

        int rearDelta = target - rearCur;
        if (rearDelta > maxStep) {
            rearDelta = maxStep;
        }
        if (rearDelta < -maxStep) {
            rearDelta = -maxStep;
        }
        rearCur += rearDelta;

        int frontTarget = (target == CAR_ESC_NEUTRAL_PWM) ? target : lagBufPush(rearCur);
        int frontDelta = frontTarget - frontCur;
        if (frontDelta > maxStep) {
            frontDelta = maxStep;
        }
        if (frontDelta < -maxStep) {
            frontDelta = -maxStep;
        }
        frontCur += frontDelta;

        applyEscPulses(rearCur, frontCur);

        pthread_mutex_lock(&motorMutex);
        currentRearEscPulseWidth  = rearCur;
        currentFrontEscPulseWidth = frontCur;
        pthread_mutex_unlock(&motorMutex);
    }
    return NULL;
}

/* ── MPU6050 gyroscope ───────────────────────────────────────────────────── */

static float gyroYawOffset = 0.0f;
static float scalingFactor = 0.40f;
static float deadZone = 1.0f;
static int   MPU6050Handle = -1;
static float currentSteeringCenter = 86.0f;

static bool  isCarTurning = false;
static float correctionAngle = 0.0f;
static float previousCorrectionAngle = 0.0f;
static pthread_mutex_t steeringWheelCorrectionMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t steeringWheelCorrectionThreadHandle;

void initMPU6050(int handle) {
    i2cWriteByteData(handle, 0x6B, 0x00);
    usleep(100000);
}

void deinitMPU6050(int handle) {
    i2cClose(handle);
}

short readMPU6050Data(int handle, int reg) {
    int high = i2cReadByteData(handle, reg);
    int low  = i2cReadByteData(handle, reg + 1);
    return (short)((high << 8) | low);
}

void calibrateMPU6050(int handle, int samples) {
    printf("[MPU6050] Calibrating Y-axis gyro (vertical heatsink mount)...\n");
    float sumY = 0.0f;
    for (int i = 0; i < samples; i++) {
        short gyroY = readMPU6050Data(handle, GYRO_YOUT_H);
        sumY += (float)gyroY / GYRO_SENSITIVITY;
        usleep(10000);
    }
    gyroYawOffset = sumY / (float)samples;
    printf("[MPU6050] Gyro Yaw offset: %.2f deg/s\n", gyroYawOffset);
}

void turnTo(const float degrees) {
    const int pulseWidth = (int)floorf(
        CAR_TURNS_MIN_PWM + (degrees / 180.0f) * (CAR_TURNS_MAX_PWM - CAR_TURNS_MIN_PWM)
    );
    gpioServo(CAR_TURNS_SERVO_PIN, pulseWidth);
    printf("[Steering] deg=%.1f pwm=%d us (pin %d)\n", degrees, pulseWidth, CAR_TURNS_SERVO_PIN);
}

void *steeringWheelCorrectionThread(void *arg) {
    int handle = *(int *)arg;

    while (1) {
        if (isCarTurning) {
            usleep(20000);
            continue;
        }

        short gyroY = readMPU6050Data(handle, GYRO_YOUT_H);
        float angularVelocityYaw = ((float)gyroY / GYRO_SENSITIVITY) - gyroYawOffset;
        float tempCorrectionAngle = 0.0f;

        if (fabs(angularVelocityYaw) > deadZone) {
            tempCorrectionAngle = -angularVelocityYaw * scalingFactor;
        }

        if (tempCorrectionAngle > MAX_CORRECTION_ANGLE) {
            tempCorrectionAngle = MAX_CORRECTION_ANGLE;
        }
        if (tempCorrectionAngle < -MAX_CORRECTION_ANGLE) {
            tempCorrectionAngle = -MAX_CORRECTION_ANGLE;
        }

        pthread_mutex_lock(&steeringWheelCorrectionMutex);

        correctionAngle = previousCorrectionAngle +
                          (tempCorrectionAngle - previousCorrectionAngle) * 0.10f;
        previousCorrectionAngle = correctionAngle;

        float currentServoAngle = currentSteeringCenter + correctionAngle;
        if (currentServoAngle > 140.0f) {
            currentServoAngle = 140.0f;
        }
        if (currentServoAngle < 0.0f) {
            currentServoAngle = 0.0f;
        }

        turnTo(currentServoAngle);
        pthread_mutex_unlock(&steeringWheelCorrectionMutex);
        usleep(20000);
    }

    return NULL;
}

/* ── ESC control ─────────────────────────────────────────────────────────── */

void move(const int speed, const char *direction) {
    int target = CAR_ESC_NEUTRAL_PWM;

    if (strcmp(direction, "forward") == 0) {
        target = CAR_ESC_NEUTRAL_PWM +
                 (int)floorf((float)speed / 100.0f * (CAR_ESC_MAX_PWM - CAR_ESC_NEUTRAL_PWM));
    } else if (strcmp(direction, "backward") == 0) {
        target = CAR_ESC_NEUTRAL_PWM -
                 (int)floorf((float)speed / 100.0f * (CAR_ESC_NEUTRAL_PWM - CAR_ESC_MIN_PWM));
    }

    if (target > CAR_ESC_MAX_PWM) {
        target = CAR_ESC_MAX_PWM;
    }
    if (target < CAR_ESC_MIN_PWM) {
        target = CAR_ESC_MIN_PWM;
    }

    printf("[ESC] cmd=%s speed=%d target=%d us\n", direction, speed, target);

    pthread_mutex_lock(&motorMutex);
    targetEscPulseWidth = target;
    pthread_mutex_unlock(&motorMutex);
}

void setEscToNeutralPosition(void) {
    lagBufInit();
    pthread_mutex_lock(&motorMutex);
    targetEscPulseWidth       = CAR_ESC_NEUTRAL_PWM;
    currentRearEscPulseWidth  = CAR_ESC_NEUTRAL_PWM;
    currentFrontEscPulseWidth = CAR_ESC_NEUTRAL_PWM;
    pthread_mutex_unlock(&motorMutex);
    applyEscPulse(CAR_ESC_NEUTRAL_PWM);
    printf("[ESC] neutral\n");
}

void enableDisableEsc(void) {
    gpioWrite(CAR_ESC_ENABLE_PIN, 0);
    printf("[ESC] enabled\n");
}

/* ── Camera ──────────────────────────────────────────────────────────────── */

void startCamera(void) {
    mediaMtxPid = fork();

    if (mediaMtxPid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (mediaMtxPid == 0) {
        execlp(getenv("MEDIAMTX_BIN_PATH"), "mediamtx",
               getenv("MEDIAMTX_CONFIG_PATH"), NULL);
        perror("execlp");
        exit(EXIT_FAILURE);
    }

    printf("[MediaMTX] started with PID %d\n", mediaMtxPid);
}

void stopCamera(void) {
    if (kill(mediaMtxPid, SIGTERM) == 0) {
        printf("[MediaMTX] sent SIGTERM to PID %d\n", mediaMtxPid);
        int status;
        waitpid(mediaMtxPid, &status, 0);
        if (WIFEXITED(status)) {
            printf("[MediaMTX] exited with status %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("[MediaMTX] killed by signal %d\n", WTERMSIG(status));
        }
    } else {
        perror("[MediaMTX] failed to terminate process");
    }
}

/* ── Camera gimbal ───────────────────────────────────────────────────────── */

void initCameraGimbal(void) {
    gpioServo(CAR_CAMERA_GIMBAL_PIN1, CAR_CAMERA_GIMBAL_MAX_PMW);
}

void cameraGimbalSetYaw(const float degrees) {
    const int pulseWidth = (int)floorf(
        ((degrees + 90.0f) / 180.0f) *
        (CAR_CAMERA_GIMBAL_MAX_PMW - CAR_CAMERA_GIMBAL_MIN_PMW) +
        CAR_CAMERA_GIMBAL_MIN_PMW
    );
    gpioServo(CAR_CAMERA_GIMBAL_PIN4, pulseWidth);
}

void cameraGimbalSetPitch(const float degrees) {
    const int pulseWidth = (int)floorf(
        ((degrees + 90.0f) / 180.0f) *
        (CAR_CAMERA_GIMBAL_MAX_PMW - CAR_CAMERA_GIMBAL_MIN_PMW) +
        CAR_CAMERA_GIMBAL_MIN_PMW
    );
    gpioServo(CAR_CAMERA_GIMBAL_PIN3, pulseWidth);
}

/* ── Unstuck (self-recovery rocking) ───────────────────────────────────────── */

static volatile bool unstuckRunning = false;
static pthread_t unstuckThreadHandle;

typedef struct { float centerAngle; } UnstuckArgs;

static void *unstuckThread(void *arg) {
    UnstuckArgs *a = (UnstuckArgs *)arg;
    float center = a->centerAngle;
    free(a);

    printf("[Unstuck] starting, center=%.1f deg\n", center);

    turnTo(center);
    usleep(300000);

    typedef struct { int speed; int fwdMs; int revMs; int neutralMs; } Cycle;
    const Cycle cycles[] = {
        { 40,  400, 400, 200 },
        { 70,  500, 500, 200 },
        { 100, 700, 700, 250 },
    };

    for (int i = 0; i < 3 && unstuckRunning; i++) {
        const Cycle *c = &cycles[i];
        printf("[Unstuck] cycle %d — speed=%d%%\n", i + 1, c->speed);

        // Forward
        int fwdTarget = CAR_ESC_NEUTRAL_PWM +
            (int)((float)c->speed / 100.0f * (CAR_ESC_MAX_PWM - CAR_ESC_NEUTRAL_PWM));
        pthread_mutex_lock(&motorMutex);
        targetEscPulseWidth = fwdTarget;
        pthread_mutex_unlock(&motorMutex);
        usleep(c->fwdMs * 1000);
        if (!unstuckRunning) {
            break;
        }

        // Neutral gap
        pthread_mutex_lock(&motorMutex);
        targetEscPulseWidth       = CAR_ESC_NEUTRAL_PWM;
        currentRearEscPulseWidth  = CAR_ESC_NEUTRAL_PWM;
        currentFrontEscPulseWidth = CAR_ESC_NEUTRAL_PWM;
        pthread_mutex_unlock(&motorMutex);
        applyEscPulse(CAR_ESC_NEUTRAL_PWM);
        usleep(c->neutralMs * 1000);
        if (!unstuckRunning) {
            break;
        }

        // Backward
        int revTarget = CAR_ESC_NEUTRAL_PWM -
            (int)((float)c->speed / 100.0f * (CAR_ESC_NEUTRAL_PWM - CAR_ESC_MIN_PWM));
        reverseArmed = true;
        pthread_mutex_lock(&motorMutex);
        targetEscPulseWidth = revTarget;
        pthread_mutex_unlock(&motorMutex);
        usleep(c->revMs * 1000);
        if (!unstuckRunning) {
            break;
        }

        // Neutral gap
        pthread_mutex_lock(&motorMutex);
        targetEscPulseWidth       = CAR_ESC_NEUTRAL_PWM;
        currentRearEscPulseWidth  = CAR_ESC_NEUTRAL_PWM;
        currentFrontEscPulseWidth = CAR_ESC_NEUTRAL_PWM;
        pthread_mutex_unlock(&motorMutex);
        applyEscPulse(CAR_ESC_NEUTRAL_PWM);
        usleep(c->neutralMs * 1000);
    }

    pthread_mutex_lock(&motorMutex);
    targetEscPulseWidth       = CAR_ESC_NEUTRAL_PWM;
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
        unstuckRunning = false;
        printf("[Unstuck] cancelled\n");
        return;
    }
    unstuckRunning = true;
    UnstuckArgs *args = malloc(sizeof(UnstuckArgs));
    if (args != NULL) {
        args->centerAngle = centerAngle;
        pthread_create(&unstuckThreadHandle, NULL, unstuckThread, args);
        pthread_detach(unstuckThreadHandle);
    }
}

/* ── MAVLink command dispatcher ──────────────────────────────────────────── */

void processMavlinkCommands(mavlink_message_t *msg) {
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
                } else {
                    initMPU6050(MPU6050Handle);
                    calibrateMPU6050(MPU6050Handle, 100);
                    if (pthread_create(&steeringWheelCorrectionThreadHandle, NULL,
                                       steeringWheelCorrectionThread, &MPU6050Handle) != 0) {
                        printf("[MPU6050] failed to create correction thread\n");
                    }
                    turnTo(currentSteeringCenter);
                    usleep(500000);
                }
                break;

            case MAVLINK_STEERING_CALIBRATION_OFF_COMMAND:
                if (MPU6050Handle >= 0) {
                    deinitMPU6050(MPU6050Handle);
                    pthread_cancel(steeringWheelCorrectionThreadHandle);
                    MPU6050Handle = -1;
                }
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
                if (unstuckRunning) {
                    unstuckRunning = false;
                }
                move((int)cmd.param1, "forward");
                break;

            case MAVLINK_BACKWARD_COMMAND:
                if (unstuckRunning) {
                    unstuckRunning = false;
                }
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
                cameraGimbalSetYaw(0.0f);
                break;

            default:
                break;
        }
    }
}

/* ── Compact 8-Byte High-Speed RC Packet Dispatcher ───────────────────────── */

static uint8_t lastFlags = 0;

void processCompactRcPacket(const CompactRcPacket *pkt) {
    if (!pkt) {
        return;
    }

    // 1. Throttle
    if (pkt->throttle > 0) {
        if (unstuckRunning) {
            unstuckRunning = false;
        }
        move((int)pkt->throttle, "forward");
    } else if (pkt->throttle < 0) {
        if (unstuckRunning) {
            unstuckRunning = false;
        }
        move((int)(-pkt->throttle), "backward");
    } else {
        setEscToNeutralPosition();
    }

    // 2. Steering
    float steerAngle = (float)pkt->steering;
    if (fabs(steerAngle - currentSteeringCenter) > 2.0f) {
        isCarTurning = true;
    } else {
        isCarTurning = false;
    }
    turnTo(steerAngle);

    // 3. Gimbal Pan / Tilt
    cameraGimbalSetYaw((float)pkt->gimbalYaw);
    cameraGimbalSetPitch((float)pkt->gimbalPitch);

    // 4. Flags (bit 0: Gyro calibration)
    bool gyroRequested = (pkt->flags & 0x01) != 0;
    bool gyroWasOn = (lastFlags & 0x01) != 0;
    if (gyroRequested && !gyroWasOn) {
        MPU6050Handle = i2cOpen(1, MPU6050_ADDRESS, 0);
        if (MPU6050Handle >= 0) {
            initMPU6050(MPU6050Handle);
            calibrateMPU6050(MPU6050Handle, 100);
            pthread_create(&steeringWheelCorrectionThreadHandle, NULL,
                           steeringWheelCorrectionThread, &MPU6050Handle);
            printf("[Gyro] Active ESP stabilization enabled\n");
        }
    } else if (!gyroRequested && gyroWasOn) {
        if (MPU6050Handle >= 0) {
            deinitMPU6050(MPU6050Handle);
            pthread_cancel(steeringWheelCorrectionThreadHandle);
            MPU6050Handle = -1;
            printf("[Gyro] Stabilization disabled\n");
        }
    }

    // Bit 1: Unstuck
    bool unstuckRequested = (pkt->flags & 0x02) != 0;
    bool unstuckWasOn = (lastFlags & 0x02) != 0;
    if (unstuckRequested && !unstuckWasOn) {
        startUnstuck(currentSteeringCenter);
    }

    lastFlags = pkt->flags;
}

/* ── Initialisation ──────────────────────────────────────────────────────── */

RcCar *newRcCar(void) {
    RcCar *rcCar = malloc(sizeof(RcCar));
    if (!rcCar) {
        return NULL;
    }

    rcCar->processMavlinkCommands = processMavlinkCommands;
    rcCar->processCompactRcPacket = processCompactRcPacket;

    if (pthread_create(&motorThreadHandle, NULL, motorThread, NULL) != 0) {
        fprintf(stderr, "[Motor] failed to create slew thread\n");
    }

    return rcCar;
}
