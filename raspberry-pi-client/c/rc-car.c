#include <cjson/cJSON.h>
#include <math.h>
#include <pigpio.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
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

typedef enum {
  TURN_TO,
  STEERING_CALIBRATION_ON,
  STEERING_CALIBRATION_OFF,
  FORWARD,
  BACKWARD,
  RESET_TURNS,
  START_CAMERA,
  STOP_CAMERA,
  CAMERA_GIMBAL_TURN_TO,
  CAMERA_GIMBAL_SET_PITCH_ANGLE,
  RESET_CAMERA_GIMBAL,
  CHANGE_DEGREE_OF_TURNS,
  INIT,
  SET_ESC_TO_NEUTRAL_POSITION,
  ACTION_UNKNOWN
} ActionType;

ActionType getActionType(const char *action) {
  if (strcmp(action, "turn-to") == 0) {
    return TURN_TO;
  } else if (strcmp(action, "reset-turns") == 0) {
    return RESET_TURNS;
  } else if (strcmp(action, "steering-calibration-on") == 0) {
    return STEERING_CALIBRATION_ON;
  } else if (strcmp(action, "steering-calibration-off") == 0) {
    return STEERING_CALIBRATION_OFF;
  } else if (strcmp(action, "change-degree-of-turns") == 0) {
    return CHANGE_DEGREE_OF_TURNS;
  } else if (strcmp(action, "forward") == 0) {
    return FORWARD;
  } else if (strcmp(action, "backward") == 0) {
    return BACKWARD;
  } else if (strcmp(action, "camera-gimbal-turn-to") == 0) {
    return CAMERA_GIMBAL_TURN_TO;
  } else if (strcmp(action, "camera-gimbal-set-pitch-angle") == 0) {
    return CAMERA_GIMBAL_SET_PITCH_ANGLE;
  } else if (strcmp(action, "reset-camera-gimbal") == 0) {
    return RESET_CAMERA_GIMBAL;
  } else if (strcmp(action, "set-esc-to-neutral-position") == 0) {
    return SET_ESC_TO_NEUTRAL_POSITION;
  } else if (strcmp(action, "init") == 0) {
    return INIT;
  } else if (strcmp(action, "start-camera") == 0) {
    return START_CAMERA;
  } else if (strcmp(action, "stop-camera") == 0) {
    return STOP_CAMERA;
  } else {
    return ACTION_UNKNOWN;
  }
}

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
  return (short)((high << 8) | low);
}

void calibrateMPU6050(int handle, int samples) {
  printf("MPU6050 Calibrating gyro...\n");
  float sumZ = 0.0;
  for (int i = 0; i < samples; i++) {
    short gyroZ = readMPU6050Data(handle, GYRO_ZOUT_H);
    sumZ += gyroZ / GYRO_SENSITIVITY;
    usleep(10000);
  }
  gyroZOffset = sumZ / samples;
  printf("MPU6050 Gyro Z Offset: %.2f\n", gyroZOffset);
}

void turnTo(const float *degrees) {
  const int pulseWidth =
      (int)floor(CAR_TURNS_MIN_PWM + ((*degrees / 180.0f) *
                                      (CAR_TURNS_MAX_PWM - CAR_TURNS_MIN_PWM)));
  gpioServo(CAR_TURNS_SERVO_PIN, pulseWidth);
}

void *steeringWheelCorrectionThread(void *arg) {
  int handle = *(int *)arg;

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

    turnTo(&currentServoAngle);
    pthread_mutex_unlock(&steeringWheelCorrectionMutex);
    usleep(20000);
  }

  return NULL;
}

void move(const int *speed, const char *direction) {
  int pulseWidth = CAR_ESC_NEUTRAL_PWM;

  if (strcmp(direction, "forward") == 0) {
    pulseWidth = (int)floorf(CAR_ESC_NEUTRAL_PWM + ((float)(*speed) / 100.0f) * (CAR_ESC_MAX_PWM - CAR_ESC_NEUTRAL_PWM));
  } else if (strcmp(direction, "backward") == 0) {
    pulseWidth = (int)floorf(CAR_ESC_NEUTRAL_PWM - ((float)(*speed) / 100.0f) * (CAR_ESC_NEUTRAL_PWM - CAR_ESC_MIN_PWM));
  }

  gpioServo(CAR_ESC_PIN, pulseWidth);
}

void setEscToNeutralPosition() { gpioServo(CAR_ESC_PIN, CAR_ESC_NEUTRAL_PWM); }

void enableDisableEsc() {
  gpioWrite(CAR_ESC_ENABLE_PIN, 1);
//  usleep(5000000);
//  gpioWrite(CAR_ESC_ENABLE_PIN, 0);
}

void startCamera() {
  mediaMtxPid = fork();

  if (mediaMtxPid == -1) {
    perror("fork");
    exit(EXIT_FAILURE);
  } else if (mediaMtxPid == 0) {
    execlp(getenv("MEDIAMTX_BIN_PATH"), "mediamtx", getenv("MEDIAMTX_CONFIG_PATH"), NULL);
    perror("execlp");
    exit(EXIT_FAILURE);
  }

  printf("MediaMTX started with PID %d \n", mediaMtxPid);
}

void stopCamera() {
  if (kill(mediaMtxPid, SIGTERM) == 0) {
    printf("MediaMTX process (PID %d) terminated successfully.\n", mediaMtxPid);
    int status;
    waitpid(mediaMtxPid, &status, 0);
    if (WIFEXITED(status)) {
      printf("MediaMTX exited with status %d.\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      printf("MediaMTX was terminated by signal %d.\n", WTERMSIG(status));
    }
  } else {
    perror("Failed to terminate MediaMTX process");
  }
}

void initCameraGimbal() {
  gpioServo(CAR_CAMERA_GIMBAL_PIN1, CAR_CAMERA_GIMBAL_MAX_PMW);
}

void cameraGimbalSetYaw(const float *degrees) {
  const int pulseWidth = (int)floorf(((*degrees + 90) / 180.0f) * (CAR_CAMERA_GIMBAL_MAX_PMW - CAR_CAMERA_GIMBAL_MIN_PMW) + CAR_CAMERA_GIMBAL_MIN_PMW);
  gpioServo(CAR_CAMERA_GIMBAL_PIN4, pulseWidth);
}

void cameraGimbalSetPitch(const float *degrees) {
  const int pulseWidth = (int)floorf(((*degrees + 90) / 180.0f) * (CAR_CAMERA_GIMBAL_MAX_PMW - CAR_CAMERA_GIMBAL_MIN_PMW) + CAR_CAMERA_GIMBAL_MIN_PMW);
  gpioServo(CAR_CAMERA_GIMBAL_PIN3, pulseWidth);
}

void processWebSocketEvents(const char *message) {
  cJSON *json = cJSON_Parse(message);
  const cJSON *data = cJSON_GetObjectItem(json, "data");
  const cJSON *rawAction = cJSON_GetObjectItem(data, "action");
  if (rawAction && cJSON_IsString(rawAction)) {
    ActionType action = getActionType(rawAction->valuestring);
    switch (action) {
      case INIT: {
        const cJSON *rawDegrees = cJSON_GetObjectItem(data, "degrees");
        const float degrees = strtof(rawDegrees->valuestring, NULL);
        turnTo(&degrees);
        enableDisableEsc();
        setEscToNeutralPosition();
        initCameraGimbal();
      } break;
      case CHANGE_DEGREE_OF_TURNS:
      case TURN_TO: {
        const cJSON *rawDegrees = cJSON_GetObjectItem(data, "degrees");
        const float degrees = strtof(rawDegrees->valuestring, NULL);
        isCarTurning = true;
        turnTo(&degrees);
      } break;
      case RESET_TURNS: {
        const cJSON *rawDegrees = cJSON_GetObjectItem(data, "degrees");
        const float degrees = strtof(rawDegrees->valuestring, NULL);
        isCarTurning = false;
        turnTo(&degrees);
      } break;
      case STEERING_CALIBRATION_ON: {
        MPU6050Handle = i2cOpen(1, MPU6050_ADDRESS, 0);
        if (MPU6050Handle < 0) {
          printf("MPU6050 Failed to open I2C connection\n");
        }

        initMPU6050(MPU6050Handle);
        calibrateMPU6050(MPU6050Handle, 100);

        if (pthread_create(&steeringWheelCorrectionThreadHandle, NULL, steeringWheelCorrectionThread, &MPU6050Handle) != 0) {
          printf("MPU6050 Failed to create correction thread\n");
        }

        float angle = 90.0f;
        turnTo(&angle);
        usleep(1000000);
      } break;
      case STEERING_CALIBRATION_OFF: {
        deinitMPU6050(MPU6050Handle);
        pthread_cancel(steeringWheelCorrectionThreadHandle);
      } break;
      case FORWARD:
      case BACKWARD: {
        const cJSON *rawDegrees = cJSON_GetObjectItem(data, "speed");
        const int speed = (int)strtof(rawDegrees->valuestring, NULL);
        move(&speed, rawAction->valuestring);
      } break;
      case SET_ESC_TO_NEUTRAL_POSITION: {
        setEscToNeutralPosition();
      } break;
      case STOP_CAMERA: {
        stopCamera();
      } break;
      case START_CAMERA: {
        startCamera();
      } break;
      case CAMERA_GIMBAL_TURN_TO: {
        const cJSON *rawDegrees = cJSON_GetObjectItem(data, "degrees");
        const float degrees = strtof(rawDegrees->valuestring, NULL);
        cameraGimbalSetYaw(&degrees);
      } break;
      case CAMERA_GIMBAL_SET_PITCH_ANGLE: {
        const cJSON *rawDegrees = cJSON_GetObjectItem(data, "degrees");
        const float degrees = strtof(rawDegrees->valuestring, NULL);
        cameraGimbalSetPitch(&degrees);
      } break;
      case RESET_CAMERA_GIMBAL: {
        const float degrees = 0;
        cameraGimbalSetYaw(&degrees);
      } break;

      default:
        break;
    }
  }

  cJSON_Delete(json);
}

RcCar *newRcCar() {
  RcCar *rcCar = (RcCar *)malloc(sizeof(RcCar));
  rcCar->processWebSocketEvents = processWebSocketEvents;
  return rcCar;
}
