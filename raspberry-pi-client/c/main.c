#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pigpio.h>
#include "libs/env/dotenv.h"
#include "websocket.h"
#include "rc-car.h"

// MPU6050 Registers
#define MPU6050_ADDR 0x68
#define PWR_MGMT_1   0x6B
#define GYRO_XOUT_H  0x43

// PID Controller Parameters
#define KP 1.5
#define KI 0.1
#define KD 0.05
#define CAR_STEERING_RANGE (CAR_TURNS_MAX_PWM - CAR_TURNS_MIN_PWM)

int isRunning = 1;

RcCar *rcCar = NULL;

// Function to initialize MPU6050
int initMPU6050() {
    int handle = i2cOpen(1, MPU6050_ADDR, 0); // I2C bus 1, MPU6050 address
    if (handle < 0) {
        fprintf(stderr, "Failed to initialize MPU6050\n");
        exit(1);
    }
    // Wake up MPU6050
    i2cWriteByteData(handle, PWR_MGMT_1, 0);
    return handle;
}

// Function to read gyro data (X-axis angular velocity)
float readGyroX(int handle) {
    // Read high and low bytes
    int16_t high = i2cReadByteData(handle, GYRO_XOUT_H);
    int16_t low = i2cReadByteData(handle, GYRO_XOUT_H + 1);

    // Check for I2C errors
    if (high < 0 || low < 0) {
        fprintf(stderr, "Error reading gyro X data\n");
        return 0.0; // Default to 0 on error
    }

    // Combine high and low bytes
    int16_t value = (high << 8) | low;

    // Convert to degrees/second
    return value / 131.0; // Scale for ±250 °/s
}

int readGyroData(int handle, int16_t *gyro_x) {
    char buf[2];

    // Read gyro X-axis high and low bytes
    if (i2cReadI2CBlockData(handle, GYRO_XOUT_H, buf, 2) < 0) {
        fprintf(stderr, "Failed to read gyro data\n");
        return -1;
    }

    *gyro_x = (buf[0] << 8) | buf[1];
    return 0;
}

void correctSteering(int handle) {
    int16_t gyro_x = 0;

    // Read gyro X-axis data
    if (readGyroData(handle, &gyro_x) < 0) {
        fprintf(stderr, "Failed to read gyro data\n");
        return;
    }

    // Calculate the midpoint of the PWM range
    int pwm_midpoint = CAR_TURNS_MIN_PWM + (CAR_STEERING_RANGE / 2);

    // Scale gyro_x to an appropriate range for steering adjustment
    // Adjust the scaling factor (e.g., 500.0) based on calibration
    int steering_pwm = pwm_midpoint + (int)(gyro_x / 500.0);

    // Clamp the PWM value to valid steering range
    if (steering_pwm > CAR_TURNS_MAX_PWM) {
        steering_pwm = CAR_TURNS_MAX_PWM;
    } else if (steering_pwm < CAR_TURNS_MIN_PWM) {
        steering_pwm = CAR_TURNS_MIN_PWM;
    }

    // Output the PWM value for debugging
    printf("steering_pwm = %d\n", steering_pwm);

    // Send the PWM signal to the steering servo
    gpioServo(CAR_TURNS_SERVO_PIN, steering_pwm);
}

#define ACCEL_XOUT_H 0x3B
#define GYRO_XOUT_H  0x43
#define ACCEL_SCALE 16384.0 // +/- 2g
#define GYRO_SCALE  131.0   // +/- 250 degrees/sec

typedef struct {
    float accel_offset_x;
    float accel_offset_y;
    float accel_offset_z;
    float gyro_offset_x;
    float gyro_offset_y;
    float gyro_offset_z;
} MPU6050_Calibration;

// Read raw data
int readRawData(int handle, int16_t *data, int start_register, int length) {
    char buf[length * 2];
    if (i2cReadI2CBlockData(handle, start_register, buf, length * 2) < 0) {
        fprintf(stderr, "Failed to read data\n");
        return -1;
    }
    for (int i = 0; i < length; i++) {
        data[i] = (buf[i * 2] << 8) | buf[i * 2 + 1];
    }
    return 0;
}

// Calibration function
void calibrateMPU6050(int handle, MPU6050_Calibration *calib, int samples) {
    int16_t accel_raw[3], gyro_raw[3];
    float accel_sum[3] = {0}, gyro_sum[3] = {0};

    for (int i = 0; i < samples; i++) {
        if (readRawData(handle, accel_raw, ACCEL_XOUT_H, 3) == 0 &&
            readRawData(handle, gyro_raw, GYRO_XOUT_H, 3) == 0) {
            for (int j = 0; j < 3; j++) {
                accel_sum[j] += accel_raw[j];
                gyro_sum[j] += gyro_raw[j];
            }
            }
        gpioDelay(1000); // 1 ms delay
    }

    // Calculate offsets
    calib->accel_offset_x = accel_sum[0] / samples;
    calib->accel_offset_y = accel_sum[1] / samples;
    calib->accel_offset_z = (accel_sum[2] / samples) - ACCEL_SCALE; // Account for gravity
    calib->gyro_offset_x = gyro_sum[0] / samples;
    calib->gyro_offset_y = gyro_sum[1] / samples;
    calib->gyro_offset_z = gyro_sum[2] / samples;
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

    // Initialize MPU6050
    int mpuHandle = initMPU6050();

    MPU6050_Calibration calib = {0};
    printf("Calibrating MPU6050... Please keep the sensor stationary.\n");
    calibrateMPU6050(mpuHandle, &calib, 1000);

    printf("Calibration complete!\n");
    printf("Accel Offsets - X: %.2f, Y: %.2f, Z: %.2f\n",
           calib.accel_offset_x, calib.accel_offset_y, calib.accel_offset_z);
    printf("Gyro Offsets - X: %.2f, Y: %.2f, Z: %.2f\n",
           calib.gyro_offset_x, calib.gyro_offset_y, calib.gyro_offset_z);

    while (isRunning) {
        lws_service(webSocketConnection.context, 100);

        correctSteering(mpuHandle);

        if (!isRunning) {
            break;
        }
    }
    return 0;
}
