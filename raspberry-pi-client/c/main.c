#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pigpio.h>
#include "libs/env/dotenv.h"
#include "websocket.h"
#include "rc-car.h"

#define MPU6050_ADDRESS 0x68
#define GYRO_ZOUT_H 0x47
#define GYRO_SENSITIVITY 131.0 // For MPU6050, sensitivity is typically 131 LSB/°/s
#define CAR_TURNS_MIN_PWM 500
#define CAR_TURNS_MAX_PWM 2500
#define MAX_CORRECTION_ANGLE 20.0 // Максимальный угол коррекции
#define NEUTRAL_ANGLE 83.0 // Нейтральный угол для серво

float gyroZOffset = 0.0; // Смещение гироскопа
float scalingFactor = 5.0; // Коэффициент коррекции
float deadZone = 0.5; // Мёртвая зона для фильтрации мелких движений

int isRunning = 1;

RcCar *rcCar = NULL;

// Initialize MPU6050
void initMPU6050(int handle) {
    i2cWriteByteData(handle, 0x6B, 0x00);  // Wake up the MPU6050
    usleep(100000);  // Wait for initialization
}

// Read 16-bit value from MPU6050
short readWord(int handle, int reg) {
    int high = i2cReadByteData(handle, reg);
    int low = i2cReadByteData(handle, reg + 1);
    return (short)((high << 8) | low);
}

// Map servo angle to pulse width
void setServoAngle(const float *degrees) {
    const int pulseWidth = (int)floor(
           CAR_TURNS_MIN_PWM +
           ((*degrees / 180.0f) * (CAR_TURNS_MAX_PWM - CAR_TURNS_MIN_PWM))
       );
    gpioServo(CAR_TURNS_SERVO_PIN, pulseWidth);
    printf("Servo Pulse Width: %d (Degrees: %.2f)\n", pulseWidth, *degrees);
}

void calibrateGyro(int handle, int samples) {
    printf("Calibrating gyro...\n");
    float sumZ = 0.0;
    for (int i = 0; i < samples; i++) {
        short gyroZ = readWord(handle, GYRO_ZOUT_H);
        sumZ += gyroZ / GYRO_SENSITIVITY;
        usleep(10000); // 10 ms delay between samples
    }
    gyroZOffset = sumZ / samples;
    printf("Gyro Z Offset: %.2f\n", gyroZOffset);
}

void handleSignal(const int signal) {
    switch (signal) {
        case SIGINT:
        case SIGTERM:
        case SIGTSTP:
            isRunning = 0;
            closeWebSocketServer();
            free(rcCar);
            exit(0);
        default:
            break;
    }
}

float smoothCorrectionAngle(float currentAngle, float targetAngle, float smoothingFactor) {
    return currentAngle + (targetAngle - currentAngle) * smoothingFactor;
}

int main() {
    if (gpioInitialise() < 0) {
        fprintf(stderr, "pigpio initialization failed\n");
        return 1;
    }

    gpioSetMode(CAR_TURNS_SERVO_PIN, PI_OUTPUT);
    gpioSetMode(CAR_ESC_PIN, PI_OUTPUT);
    gpioSetMode(CAR_CAMERA_GIMBAL_PIN1, PI_OUTPUT);
    gpioSetMode(CAR_CAMERA_GIMBAL_PIN3, PI_OUTPUT);
    gpioSetMode(CAR_CAMERA_GIMBAL_PIN4, PI_OUTPUT);

    rcCar = newRcCar();
    env_load(".env", false);

    struct sigaction sa;
    WebSocketConnection webSocketConnection = connectToWebSocketServer();

    sa.sa_handler = handleSignal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);

    setWebSocketEventCallback(rcCar->processWebSocketEvents);


    // Open I2C connection to MPU6050
    int handle = i2cOpen(1, MPU6050_ADDRESS, 0);
    if (handle < 0) {
        printf("Failed to open I2C connection\n");
        gpioTerminate();
        return -1;
    }

    // Initialize MPU6050
    initMPU6050(handle);

    // Calibration offset for gyroscope bias
    float gyroZOffset = 0.0;

    calibrateGyro(handle, 100);

    float angle = 83.0f;
    setServoAngle(&angle);
    usleep(1000000);

    float previousCorrectionAngle = 0.0;


    while (isRunning) {
//        lws_service(webSocketConnection.context, 100);

        short gyroZ = readWord(handle, GYRO_ZOUT_H);
        float angularVelocityZ = (gyroZ / GYRO_SENSITIVITY) - gyroZOffset;

        // Рассчитываем целевой угол коррекции
        float correctionAngle = 0.0;
        if (fabs(angularVelocityZ) > deadZone) {
            correctionAngle = -angularVelocityZ * scalingFactor;
        }

        // Ограничиваем угол коррекции
        if (correctionAngle > MAX_CORRECTION_ANGLE) correctionAngle = MAX_CORRECTION_ANGLE;
        if (correctionAngle < -MAX_CORRECTION_ANGLE) correctionAngle = -MAX_CORRECTION_ANGLE;

        // Применяем сглаживание
        float smoothedCorrectionAngle = smoothCorrectionAngle(previousCorrectionAngle, correctionAngle, 0.05f);
        previousCorrectionAngle = smoothedCorrectionAngle;

        // Рассчитываем текущий угол серво
        float currentServoAngle = NEUTRAL_ANGLE + smoothedCorrectionAngle;
        if (currentServoAngle > 180.0) currentServoAngle = 180.0;
        if (currentServoAngle < 0.0) currentServoAngle = 0.0;

        // Устанавливаем угол серво
        setServoAngle(&currentServoAngle);

        // Логирование
        printf("Gyro Z: %.2f, Smoothed Correction Angle: %.2f, Servo Angle: %.2f\n",
               angularVelocityZ, smoothedCorrectionAngle, currentServoAngle);

        usleep(50000); // 50 мс
        if (!isRunning) {
            break;
        }
    }
    return 0;
}
