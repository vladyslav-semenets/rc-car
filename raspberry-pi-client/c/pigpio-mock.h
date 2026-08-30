// pigpio.h (mock)
#ifndef PIGPIO_H
#define PIGPIO_H

#define PI_OUTPUT 1
#define PI_INPUT 0

int gpioInitialise(void);
void gpioTerminate(void);
int gpioSetMode(unsigned gpio, unsigned mode);
int gpioWrite(unsigned gpio, unsigned level);
int gpioPWM(unsigned gpio, unsigned dutycycle);
int gpioServo(unsigned gpio, unsigned pulsewidth);
int i2cOpen(unsigned bus, unsigned addr, unsigned flags);
int i2cClose(unsigned handle);
int i2cReadByteData(unsigned handle, unsigned reg);
int i2cWriteByteData(unsigned handle, unsigned reg, unsigned byte);

#endif
