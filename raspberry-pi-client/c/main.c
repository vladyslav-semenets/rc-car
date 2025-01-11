#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <pigpio.h>
#include "libs/env/dotenv.h"
#include "websocket.h"
#include "rc-car.h"
#include <pthread.h>

#define MPU6050_ADDRESS 0x68
#define GYRO_XOUT_H 0x43
#define GYRO_YOUT_H 0x45
#define ACCEL_XOUT_H 0x3B
#define ACCEL_YOUT_H 0x3D
#define ACCEL_ZOUT_H 0x3F
#define GYRO_SENSITIVITY 131.0f // Гироскоп: 131 LSB/°/s
#define ACCEL_SENSITIVITY 16384.0f // Акселерометр: 16384 LSB/g
#define MAX_CORRECTION_ANGLE 20.0f // Максимальный угол коррекции
#define NEUTRAL_ANGLE 90.0f       // Нейтральный угол сервопривода

float gyroYOffset = 0.0f; // Смещение гироскопа по Y
float scalingFactor = 15.0f; // Коэффициент коррекции
float deadZone = 0.5f; // Мертвая зона для фильтрации мелких движений
int isRunning = 1;

// Shared variables
float correctionAngle = 0.0f;
float previousCorrectionAngle = 0.0f;
pthread_mutex_t correctionMutex = PTHREAD_MUTEX_INITIALIZER;

// Map servo angle to pulse width
void setServoAngle(const float *degrees) {
    const int pulseWidth = (int)floor(
        CAR_TURNS_MIN_PWM + ((*degrees / 180.0f) * (CAR_TURNS_MAX_PWM - CAR_TURNS_MIN_PWM))
    );
    gpioServo(CAR_TURNS_SERVO_PIN, pulseWidth);
    printf("Servo Pulse Width: %d (Degrees: %.2f)\n", pulseWidth, *degrees);
}

// Calculate pitch from accelerometer data
float calculatePitch(short accelX, short accelY, short accelZ) {
    return atan2f((float)accelY, sqrtf((float)(accelX * accelX + accelZ * accelZ))) * 180.0f / M_PI;
}

// Complementary filter
float complementaryFilter(float prevAngle, float gyroRate, float accelAngle, float alpha, float deltaTime) {
    return alpha * (prevAngle + gyroRate * deltaTime) + (1.0f - alpha) * accelAngle;
}

// Read 16-bit value from MPU6050
short readWord(int handle, int reg) {
    int high = i2cReadByteData(handle, reg);
    int low = i2cReadByteData(handle, reg + 1);
    return (short)((high << 8) | low);
}

// Calibrate the gyro to calculate offsets
void calibrateGyro(int handle, int samples) {
    printf("Calibrating gyro...\n");
    float sumY = 0.0;
    for (int i = 0; i < samples; i++) {
        short gyroY = readWord(handle, GYRO_YOUT_H);
        sumY += gyroY / GYRO_SENSITIVITY;
        usleep(10000); // 10 ms delay between samples
    }
    gyroYOffset = sumY / samples;
    printf("Gyro Y Offset: %.2f\n", gyroYOffset);
}

// Thread function for correction logic
void* correctionThread(void *arg) {
    int handle = *(int *)arg;
    float pitch = 0.0f; // Текущий угол наклона
    float gyroRatePitch = 0.0f;

    while (isRunning) {
        // Read accelerometer and gyroscope data
        short accelX = readWord(handle, ACCEL_XOUT_H);
        short accelY = readWord(handle, ACCEL_YOUT_H);
        short accelZ = readWord(handle, ACCEL_ZOUT_H);
        short gyroY = readWord(handle, GYRO_YOUT_H);

        // Convert gyro data to angular velocity
        gyroRatePitch = gyroY / GYRO_SENSITIVITY - gyroYOffset;

        // Calculate pitch from accelerometer
        float accelPitch = calculatePitch(accelX, accelY, accelZ);

        // Apply complementary filter
        pitch = complementaryFilter(pitch, gyroRatePitch, accelPitch, 0.98f, 0.02f);

        // Calculate correction angle
        pthread_mutex_lock(&correctionMutex);
        correctionAngle = -pitch * scalingFactor;

        // Clamp correction angle
        if (correctionAngle > MAX_CORRECTION_ANGLE) correctionAngle = MAX_CORRECTION_ANGLE;
        if (correctionAngle < -MAX_CORRECTION_ANGLE) correctionAngle = -MAX_CORRECTION_ANGLE;

        // Update servo angle
        float currentServoAngle = NEUTRAL_ANGLE + correctionAngle;
        if (currentServoAngle > 180.0f) currentServoAngle = 180.0f;
        if (currentServoAngle < 0.0f) currentServoAngle = 0.0f;

        setServoAngle(&currentServoAngle);
        pthread_mutex_unlock(&correctionMutex);

        usleep(20000); // Sleep for 20 ms
    }
    return NULL;
}

// Signal handling for clean shutdown
void handleSignal(const int signal) {
    if (signal == SIGINT || signal == SIGTERM || signal == SIGTSTP) {
        isRunning = 0;
        gpioTerminate();
        exit(0);
    }
}

int main() {
    if (gpioInitialise() < 0) {
        fprintf(stderr, "pigpio initialization failed\n");
        return 1;
    }

    gpioSetMode(CAR_TURNS_SERVO_PIN, PI_OUTPUT);

    struct sigaction sa;
    sa.sa_handler = handleSignal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);

    // Open I2C connection to MPU6050
    int handle = i2cOpen(1, MPU6050_ADDRESS, 0);
    if (handle < 0) {
        fprintf(stderr, "Failed to open I2C connection\n");
        gpioTerminate();
        return -1;
    }

    // Initialize MPU6050
    i2cWriteByteData(handle, 0x6B, 0x00); // Wake up the MPU6050
    usleep(100000); // Wait for initialization

    // Calibrate gyro
    calibrateGyro(handle, 100);

    // Start correction thread
    pthread_t correctionThreadHandle;
    if (pthread_create(&correctionThreadHandle, NULL, correctionThread, &handle) != 0) {
        fprintf(stderr, "Failed to create correction thread\n");
        gpioTerminate();
        return -1;
    }

    float angle = 90.0f;  // Start with neutral position
    setServoAngle(&angle);
    usleep(1000000);

    // Main loop (placeholder)
    while (isRunning) {
        usleep(100000); // Keep the program running
    }

    // Cleanup
    pthread_join(correctionThreadHandle, NULL);
    gpioTerminate();
    return 0;
}
