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
    int16_t high = i2cReadByteData(handle, GYRO_XOUT_H);
    int16_t low = i2cReadByteData(handle, GYRO_XOUT_H + 1);
    int16_t value = (high << 8) | low;

    // Convert to degrees/second
    if (value > 32768) value -= 65536;
    return value / 131.0;
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

    while (isRunning) {
//        lws_service(webSocketConnection.context, 100);

        float gyroRate = readGyroX(mpuHandle);

        printf("Gyro X: %f\n", gyroRate);

        sleep(5000);
        if (!isRunning) {
            break;
        }
    }
    return 0;
}
