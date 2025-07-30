// pigpio.c (mock)
#include <stdio.h>
#include "pigpio-mock.h"

int gpioInitialise(void) {
    printf("Mock: gpioInitialise()\n");
    return 0; // 0 = OK
}

void gpioTerminate(void) {
    printf("Mock: gpioTerminate()\n");
}

int gpioSetMode(unsigned gpio, unsigned mode) {
    printf("Mock: gpioSetMode(pin %u, mode %u)\n", gpio, mode);
    return 0;
}

int gpioWrite(unsigned gpio, unsigned level) {
    printf("Mock: gpioWrite(pin %u, level %u)\n", gpio, level);
    return 0;
}

int gpioPWM(unsigned gpio, unsigned dutycycle) {
    printf("Mock: gpioPWM(pin %u, dutycycle %u)\n", gpio, dutycycle);
    return 0;
}

int gpioServo(unsigned gpio, unsigned pulsewidth) {
    printf("Mock: gpioServo(pin %u, pulsewidth %u)\n", gpio, pulsewidth);
    return 0;
}

int i2cOpen(unsigned bus, unsigned addr, unsigned flags) {
    printf("Mock: i2cOpen(bus=%u, addr=0x%02X, flags=%u)\n", bus, addr, flags);
    return 1;  // Simulated handle
}

int i2cClose(unsigned handle) {
    printf("Mock: i2cClose(handle=%u)\n", handle);
    return 0;
}

int i2cReadByteData(unsigned handle, unsigned reg) {
    printf("Mock: i2cReadByteData(handle=%u, reg=0x%02X)\n", handle, reg);
    return 0x42;  // Return dummy byte
}

int i2cWriteByteData(unsigned handle, unsigned reg, unsigned byte) {
    printf("Mock: i2cWriteByteData(handle=%u, reg=0x%02X, byte=0x%02X)\n", handle, reg, byte);
    return 0;
}
