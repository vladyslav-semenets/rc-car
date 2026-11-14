#ifndef INA3221_H
#define INA3221_H

#include <stdbool.h>
#include <stdint.h>

#define INA3221_I2C_ADDR     0x40
#define INA3221_REG_CONFIG   0x00
#define INA3221_REG_VBUS_CH1 0x02
#define INA3221_REG_VBUS_CH2 0x04
#define INA3221_REG_VBUS_CH3 0x06

/* CH1=1, CH2=1, CH3=0 | AVG=011 (64 samples) | VBUS_CT=100 (1.1 ms) | MODE=110 (Bus continuous) */
#define INA3221_CONFIG_VAL   0x6706

/* 2S LiPo voltage thresholds (V) */
#define LIPO_2S_MAX_V        8.40f
#define LIPO_2S_WARN_V       6.80f
#define LIPO_2S_CRIT_V       6.40f

/**
 * Open and configure INA3221 on the given I2C device path (or auto-probe if NULL).
 *
 * @param devPath Optional I2C device path (e.g. "/dev/i2c-3" or "/dev/i2c-1").
 * @return File descriptor >= 0 on success, -1 on failure.
 */
int ina3221_open(const char *devPath);

/**
 * Close the INA3221 I2C file descriptor.
 */
void ina3221_close(int fd);

/**
 * Read Bus Voltage in Volts for a channel register (INA3221_REG_VBUS_CH1..CH3).
 *
 * @return 0 on success, -1 on error.
 */
int ina3221_read_bus_voltage(int fd, uint8_t reg, float *voltageOut);

/**
 * Estimate 2S LiPo state of charge (0..100%).
 */
int ina3221_estimate_lipo_2s_percent(float voltage);

#endif // INA3221_H
