#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pigpio.h>
#include <pthread.h>
#include <math.h>
#include "libs/env/dotenv.h"
#include "websocket.h"
#include "rc-car.h"
#include <stdbool.h>

#define MPU6050_ADDRESS 0x68
#define ACCEL_XOUT_H 0x3B
#define ACCEL_YOUT_H 0x3D
#define ACCEL_ZOUT_H 0x3F
#define GYRO_ZOUT_H 0x47
#define CAR_TURNS_SERVO_PIN 17
#define CAR_TURNS_MIN_PWM 500
#define CAR_TURNS_MAX_PWM 1800
#define NEUTRAL_ANGLE 90.0
#define MAX_CORRECTION_ANGLE 20.0 // Максимальный угол коррекции

int isRunning = 1;
RcCar *rcCar = NULL;
float gyroZOffset = 0.0; // Смещение гироскопа
pthread_mutex_t servoMutex = PTHREAD_MUTEX_INITIALIZER;

// Инициализация MPU6050
void initMPU6050(int handle) {
    i2cWriteByteData(handle, 0x6B, 0x00);  // Wake up MPU6050
    usleep(100000);
}

// Чтение 16-битного значения с MPU6050
short readWord(int handle, int reg) {
    int high = i2cReadByteData(handle, reg);
    int low = i2cReadByteData(handle, reg + 1);
    return (short)((high << 8) | low);
}

// Калибровка гироскопа
void calibrateGyro(int handle, int samples) {
    printf("Calibrating gyro...\n");
    float sumZ = 0.0;
    for (int i = 0; i < samples; i++) {
        short gyroZ = readWord(handle, GYRO_ZOUT_H);
        sumZ += gyroZ;
        usleep(10000); // 10 ms delay
    }
    gyroZOffset = sumZ / samples;
    printf("Gyro Z Offset: %.2f\n", gyroZOffset);
}

// Установка угла серво
void setServoAngle(float angle) {
    if (angle < 0.0) angle = 0.0;
    if (angle > 180.0) angle = 180.0;

    int pulseWidth = (int)(CAR_TURNS_MIN_PWM +
                           (angle / 180.0) * (CAR_TURNS_MAX_PWM - CAR_TURNS_MIN_PWM));
    gpioServo(CAR_TURNS_SERVO_PIN, pulseWidth);
    printf("Servo set to %.2f degrees (Pulse Width: %d)\n", angle, pulseWidth);
}

// Рассчёт угла на основе акселерометра
float calculateAngleFromAccel(int handle) {
    short accelX = readWord(handle, ACCEL_XOUT_H);
    short accelZ = readWord(handle, ACCEL_ZOUT_H);
    float angle = atan2f((float)accelX, (float)accelZ) * (180.0 / M_PI);
    return angle;
}

// Поток управления серво
void* servoControlThread(void* arg) {
    int handle = *(int*)arg;

    while (isRunning) {
        // Читаем угол из акселерометра
        float currentAngle = calculateAngleFromAccel(handle);
        float correctionAngle = NEUTRAL_ANGLE - currentAngle;

        // Ограничиваем угол коррекции
        if (correctionAngle > MAX_CORRECTION_ANGLE) correctionAngle = MAX_CORRECTION_ANGLE;
        if (correctionAngle < -MAX_CORRECTION_ANGLE) correctionAngle = -MAX_CORRECTION_ANGLE;

        // Рассчитываем итоговый угол для сервопривода
        float servoAngle = NEUTRAL_ANGLE + correctionAngle;

        // Устанавливаем угол сервопривода
        pthread_mutex_lock(&servoMutex);
        setServoAngle(servoAngle);
        pthread_mutex_unlock(&servoMutex);

        //usleep(20000);  // Задержка 20 мс
    }
    return NULL;
}

// Обработчик сигналов
void handleSignal(int signal) {
    isRunning = 0;
    gpioTerminate();
    exit(0);
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

    // Установка пина для серво
    gpioSetMode(CAR_TURNS_SERVO_PIN, PI_OUTPUT);

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
        fprintf(stderr, "Failed to open I2C connection\n");
        gpioTerminate();
        return 1;
    }

    initMPU6050(handle);
    calibrateGyro(handle, 100);

    // Создаём поток управления серво
    pthread_t servoThread;
    if (pthread_create(&servoThread, NULL, servoControlThread, &handle) != 0) {
        fprintf(stderr, "Failed to create servo control thread\n");
        gpioTerminate();
        return 1;
    }

    // Основной цикл
    while (isRunning) {
        lws_service(webSocketConnection.context, 100);
    }

    pthread_join(servoThread, NULL);
    gpioTerminate();
    return 0;
}
