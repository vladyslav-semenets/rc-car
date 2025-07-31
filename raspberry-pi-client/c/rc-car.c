#include <math.h>
#include "libs/mavlink/common/mavlink.h"
#ifdef __APPLE__
// Include mock pigpio.h
#include "pigpio-mock.h"
#else
// Use real pigpio
#include <pigpio.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "stdbool.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "rc-car.h"
#include "websocket.h"

pid_t mediaMtxPid = -1;

float gyroZOffset = 0.0;
float scalingFactor = 15.0;
float deadZone = 0.5;
int MPU6050Handle = -1;

bool isCarTurning = false;
float correctionAngle = 0.0;
float previousCorrectionAngle = 0.0;
pthread_mutex_t steeringWheelCorrectionMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t steeringWheelCorrectionThreadHandle;

void initMPU6050(int handle) {
    i2cWriteByteData(handle, 0x6B, 0x00);
    usleep(100000);
}

void deinitMPU6050(int handle) {
    i2cClose(handle);
}

short readMPU6050Data(int handle, int reg) {
    int high = i2cReadByteData(handle, reg);
    int low = i2cReadByteData(handle, reg + 1);
    return (short) ((high << 8) | low);
}

void calibrateMPU6050(int handle, int samples) {
    printf("[MPU6050] Calibrating gyro...\n");
    float sumZ = 0.0;
    for (int i = 0; i < samples; i++) {
        short gyroZ = readMPU6050Data(handle, GYRO_ZOUT_H);
        sumZ += gyroZ / GYRO_SENSITIVITY;
        usleep(10000);
    }
    gyroZOffset = sumZ / samples;
    printf("[MPU6050] Gyro Z Offset: %.2f\n", gyroZOffset);
}

void turnTo(const float degrees) {
    const int pulseWidth =
            (int) floorf(CAR_TURNS_MIN_PWM + ((degrees / 180.0f) *
                                              (CAR_TURNS_MAX_PWM - CAR_TURNS_MIN_PWM)));
    gpioServo(CAR_TURNS_SERVO_PIN, pulseWidth);
}

void *steeringWheelCorrectionThread(void *arg) {
    int handle = *(int *) arg;

    while (1) {
        if (isCarTurning) {
            continue;
        }

        short gyroZ = readMPU6050Data(handle, GYRO_ZOUT_H);
        float angularVelocityZ = (gyroZ / GYRO_SENSITIVITY) - gyroZOffset;
        float tempCorrectionAngle = 0.0;

        if (fabs(angularVelocityZ) > deadZone) {
            tempCorrectionAngle = -angularVelocityZ * scalingFactor;
        }

        if (tempCorrectionAngle > MAX_CORRECTION_ANGLE) {
            tempCorrectionAngle = MAX_CORRECTION_ANGLE;
        }

        if (tempCorrectionAngle < -MAX_CORRECTION_ANGLE) {
            tempCorrectionAngle = -MAX_CORRECTION_ANGLE;
        }

        pthread_mutex_lock(&steeringWheelCorrectionMutex);

        correctionAngle = previousCorrectionAngle + (tempCorrectionAngle - previousCorrectionAngle) * 0.05f;
        previousCorrectionAngle = correctionAngle;

        float currentServoAngle = NEUTRAL_ANGLE + correctionAngle;

        if (currentServoAngle > 180.0) {
            currentServoAngle = 180.0;
        }

        if (currentServoAngle < 0.0) {
            currentServoAngle = 0.0;
        }

        turnTo(currentServoAngle);
        pthread_mutex_unlock(&steeringWheelCorrectionMutex);
        usleep(20000);
    }

    return NULL;
}

void move(const int speed, const char *direction) {
    printf("speed: %d, direction: %s", speed, direction);
    int pulseWidth = CAR_ESC_NEUTRAL_PWM;

    if (strcmp(direction, "forward") == 0) {
        pulseWidth = (int) floorf(
            CAR_ESC_NEUTRAL_PWM + ((float) (speed) / 100.0f) * (CAR_ESC_MAX_PWM - CAR_ESC_NEUTRAL_PWM));
    } else if (strcmp(direction, "backward") == 0) {
        pulseWidth = (int) floorf(
            CAR_ESC_NEUTRAL_PWM - ((float) (speed) / 100.0f) * (CAR_ESC_NEUTRAL_PWM - CAR_ESC_MIN_PWM));
    }

    gpioServo(CAR_ESC_PIN, pulseWidth);
}

void setEscToNeutralPosition() { gpioServo(CAR_ESC_PIN, CAR_ESC_NEUTRAL_PWM); }

void enableDisableEsc() {
    gpioWrite(CAR_ESC_ENABLE_PIN, 1);
    printf("[ESC] OFF\n");
    sleep(5);
    gpioWrite(CAR_ESC_ENABLE_PIN, 0);
    printf("[ESC] ON\n");
    sleep(5);
}

void startCamera() {
    mediaMtxPid = fork();

    if (mediaMtxPid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (mediaMtxPid == 0) {
        execlp(getenv("MEDIAMTX_BIN_PATH"), "mediamtx", getenv("MEDIAMTX_CONFIG_PATH"), NULL);
        perror("execlp");
        exit(EXIT_FAILURE);
    }

    printf("[MediaMTX] started with PID %d \n", mediaMtxPid);
}

void stopCamera() {
    if (kill(mediaMtxPid, SIGTERM) == 0) {
        printf("[MediaMTX] process (PID %d) terminated successfully.\n", mediaMtxPid);
        int status;
        waitpid(mediaMtxPid, &status, 0);
        if (WIFEXITED(status)) {
            printf("[MediaMTX] exited with status %d.\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("[MediaMTX] was terminated by signal %d.\n", WTERMSIG(status));
        }
    } else {
        perror("[MediaMTX] Failed to terminate process");
    }
}

void initCameraGimbal() {
    gpioServo(CAR_CAMERA_GIMBAL_PIN1, CAR_CAMERA_GIMBAL_MAX_PMW);
}

void cameraGimbalSetYaw(const float degrees) {
    const int pulseWidth = (int) floorf(
        ((degrees + 90) / 180.0f) * (CAR_CAMERA_GIMBAL_MAX_PMW - CAR_CAMERA_GIMBAL_MIN_PMW) +
        CAR_CAMERA_GIMBAL_MIN_PMW);
    gpioServo(CAR_CAMERA_GIMBAL_PIN4, pulseWidth);
}

void cameraGimbalSetPitch(const float degrees) {
    const int pulseWidth = (int) floorf(
        ((degrees + 90) / 180.0f) * (CAR_CAMERA_GIMBAL_MAX_PMW - CAR_CAMERA_GIMBAL_MIN_PMW) +
        CAR_CAMERA_GIMBAL_MIN_PMW);
    gpioServo(CAR_CAMERA_GIMBAL_PIN3, pulseWidth);
}

void processMavlinkCommands(mavlink_message_t *msg) {
    if (msg->msgid == MAVLINK_MSG_ID_COMMAND_LONG) {
        mavlink_command_long_t commandLong;
        mavlink_msg_command_long_decode(msg, &commandLong);

        switch (commandLong.command) {
            case MAVLINK_INIT_COMMAND: {
                turnTo(commandLong.param2);
                enableDisableEsc();
                setEscToNeutralPosition();
                initCameraGimbal();
            }
            break;
            case MAVLINK_CHANGE_DEGREE_OF_TURNS_COMMAND:
            case MAVLINK_TURN_TO_COMMAND: {
                isCarTurning = true;
                turnTo(commandLong.param1);
            }
            break;
            case MAVLINK_RESET_TURNS_COMMAND: {
                isCarTurning = false;
                turnTo(commandLong.param1);
            }
            break;
            case MAVLINK_STEERING_CALIBRATION_ON_COMMAND: {
                MPU6050Handle = i2cOpen(1, MPU6050_ADDRESS, 0);
                if (MPU6050Handle < 0) {
                    printf("MPU6050 Failed to open I2C connection\n");
                }

                initMPU6050(MPU6050Handle);
                calibrateMPU6050(MPU6050Handle, 100);

                if (pthread_create(&steeringWheelCorrectionThreadHandle, NULL, steeringWheelCorrectionThread,
                                   &MPU6050Handle) != 0) {
                    printf("MPU6050 Failed to create correction thread\n");
                }

                const float angle = 90.0f;
                turnTo(angle);
                usleep(1000000);
            }
            break;
            case MAVLINK_STEERING_CALIBRATION_OFF_COMMAND: {
                deinitMPU6050(MPU6050Handle);
                pthread_cancel(steeringWheelCorrectionThreadHandle);
            }
            break;
            case MAVLINK_FORWARD_COMMAND: {
                move((int) commandLong.param1, "forward");
            }
            break;
            case MAVLINK_BACKWARD_COMMAND: {
                move((int) commandLong.param1, "backward");
            }
            break;
            case MAVLINK_SET_ESC_TO_NEUTRAL_POSITION_COMMAND: {
                setEscToNeutralPosition();
            }
            break;
            case MAVLINK_START_CAMERA_COMMAND: {
                stopCamera();
            }
            break;
            case MAVLINK_STOP_CAMERA_COMMAND: {
                startCamera();
            }
            break;
            case MAVLINK_CAMERA_GIMBAL_TURN_TO_COMMAND: {
                cameraGimbalSetYaw(commandLong.param1);
            }
            break;
            case MAVLINK_CAMERA_GIMBAL_SET_PITCH_ANGLE_COMMAND: {
                cameraGimbalSetPitch(commandLong.param1);
            }
            break;
            case MAVLINK_RESET_CAMERA_GIMBAL_COMMAND: {
                cameraGimbalSetYaw(0);
            }
            break;

            default:
                break;
        }
    }
}

RcCar *newRcCar() {
    RcCar *rcCar = malloc(sizeof(RcCar));
    rcCar->processMavlinkCommands = processMavlinkCommands;
    return rcCar;
}
