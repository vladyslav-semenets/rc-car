#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pigpio.h>
#include "libs/env/dotenv.h"
#include "websocket.h"
#include "rc-car.h"

#define MPU6050_ADDR 0x68  // I2C address of MPU6050
#define PWR_MGMT_1 0x6B
#define GYRO_ZOUT_H 0x47

#define STEERING_SERVO_PIN 17  // GPIO pin for the steering servo
#define MIN_SERVO_PULSE_WIDTH 500   // Minimum pulse width (0.5 ms)
#define MAX_SERVO_PULSE_WIDTH 2500  // Maximum pulse width (2.5 ms)
#define SERVO_CENTER 83             // Neutral steering position in degrees
#define SERVO_NEUTRAL_ANGLE 83  // Neutral steering angle (degrees)

// Sensitivity of the gyroscope
#define GYRO_SENSITIVITY 131.0  // Unit: degrees/sec

// Maximum correction angle for the servo
#define MAX_CORRECTION_ANGLE 30.0  // In degrees

int isRunning = 1;

RcCar *rcCar = NULL;

// Initialize MPU6050
void initMPU6050(int handle) {
    i2cWriteByteData(handle, PWR_MGMT_1, 0x00);  // Wake up the MPU6050
    usleep(100000);  // Wait for initialization
}

// Read 16-bit value from MPU6050
short readWord(int handle, int reg) {
    int high = i2cReadByteData(handle, reg);
    int low = i2cReadByteData(handle, reg + 1);
    return (short)((high << 8) | low);
}

// Map servo angle to pulse width
void setServoAngle(int gpioPin, float angle) {
    int pulseWidth;

    // Adjust the angle around the neutral position
    float adjustedAngle = SERVO_NEUTRAL_ANGLE + angle;

    // Map adjusted angle to pulse width (500-2500 µs)
    pulseWidth = MIN_SERVO_PULSE_WIDTH +
                 ((adjustedAngle - SERVO_NEUTRAL_ANGLE) / MAX_CORRECTION_ANGLE) *
                 (MAX_SERVO_PULSE_WIDTH - MIN_SERVO_PULSE_WIDTH);

    // Clamp pulse width to valid range
    if (pulseWidth < MIN_SERVO_PULSE_WIDTH) pulseWidth = MIN_SERVO_PULSE_WIDTH;
    if (pulseWidth > MAX_SERVO_PULSE_WIDTH) pulseWidth = MAX_SERVO_PULSE_WIDTH;

    // Set the servo position
    gpioServo(gpioPin, pulseWidth);

    printf("Angle: %.2f, Adjusted Angle: %.2f, Pulse Width: %d\n", angle, adjustedAngle, pulseWidth);
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


    // Open I2C connection to MPU6050
    int handle = i2cOpen(1, MPU6050_ADDR, 0);
    if (handle < 0) {
        printf("Failed to open I2C connection\n");
        gpioTerminate();
        return -1;
    }

    // Initialize MPU6050
    initMPU6050(handle);

    // Calibration offset for gyroscope bias
    float gyroZOffset = 0.0;

    // Gyroscope calibration
    for (int i = 0; i < 100; i++) {
        short gyroZ = readWord(handle, GYRO_ZOUT_H);
        gyroZOffset += gyroZ / GYRO_SENSITIVITY;
        usleep(10000);  // 10 ms delay
    }
    gyroZOffset /= 100.0;  // Average offset
    printf("Gyro Z Offset: %.2f\n", gyroZOffset);

    setServoAngle(STEERING_SERVO_PIN, 83.0f);

    while (isRunning) {
//        lws_service(webSocketConnection.context, 100);

        // Read angular velocity from the gyroscope (Z-axis)
        short gyroZ = readWord(handle, GYRO_ZOUT_H);
        float angularVelocityZ = (gyroZ / GYRO_SENSITIVITY) - gyroZOffset;

        float correctionAngle = -angularVelocityZ;

        // Clamp correction angle
        if (correctionAngle > MAX_CORRECTION_ANGLE) correctionAngle = MAX_CORRECTION_ANGLE;
        if (correctionAngle < -MAX_CORRECTION_ANGLE) correctionAngle = -MAX_CORRECTION_ANGLE;

//        setServoAngle(STEERING_SERVO_PIN, correctionAngle);
//        usleep(100000);  // 100 ms delay

        if (!isRunning) {
            break;
        }
    }
    return 0;
}
