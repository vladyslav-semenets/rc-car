#include "ina3221.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __linux__
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

static int ina3221_write_reg(int fd, uint8_t reg, uint16_t value) {
    uint8_t buf[3];
    buf[0] = reg;
    buf[1] = (uint8_t)((value >> 8) & 0xFF); /* MSByte */
    buf[2] = (uint8_t)(value & 0xFF);        /* LSByte */

    if (write(fd, buf, 3) != 3) {
        return -1;
    }
    return 0;
}

static int ina3221_try_open_dev(const char *path) {
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        return -1;
    }

    if (ioctl(fd, I2C_SLAVE, INA3221_I2C_ADDR) < 0) {
        close(fd);
        return -1;
    }

    if (ina3221_write_reg(fd, INA3221_REG_CONFIG, INA3221_CONFIG_VAL) < 0) {
        close(fd);
        return -1;
    }

    printf("[INA3221] Initialized on %s (addr 0x%02X)\n", path, INA3221_I2C_ADDR);
    return fd;
}

int ina3221_open(const char *devPath) {
    if (devPath != NULL && devPath[0] != '\0') {
        return ina3221_try_open_dev(devPath);
    }

    const char *candidates[] = {"/dev/i2c-3", "/dev/i2c-1", "/dev/i2c-4", "/dev/i2c-0"};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        int fd = ina3221_try_open_dev(candidates[i]);
        if (fd >= 0) {
            return fd;
        }
    }

    return -1;
}

void ina3221_close(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

int ina3221_read_bus_voltage(int fd, uint8_t reg, float *voltageOut) {
    if (fd < 0 || voltageOut == NULL) {
        return -1;
    }

    if (write(fd, &reg, 1) != 1) {
        return -1;
    }

    uint8_t buf[2] = {0};
    if (read(fd, buf, 2) != 2) {
        return -1;
    }

    int16_t raw = (int16_t)((buf[0] << 8) | buf[1]);
    *voltageOut = (float)(raw >> 3) * 0.008f;
    return 0;
}
#else
int ina3221_open(const char *devPath) {
    (void)devPath;
    return -1;
}

void ina3221_close(int fd) {
    (void)fd;
}

int ina3221_read_bus_voltage(int fd, uint8_t reg, float *voltageOut) {
    (void)fd;
    (void)reg;
    (void)voltageOut;
    return -1;
}
#endif

int ina3221_estimate_lipo_2s_percent(float voltage) {
    if (voltage >= LIPO_2S_MAX_V) return 100;
    if (voltage <= LIPO_2S_CRIT_V) return 0;
    return (int)(((voltage - LIPO_2S_CRIT_V) / (LIPO_2S_MAX_V - LIPO_2S_CRIT_V)) * 100.0f);
}
