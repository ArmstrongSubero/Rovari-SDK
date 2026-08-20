/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari - rvembedded.com
 *
 * rovari_i2c.h - I2C abstraction for Baochip-1x
 *
 * Dabao board I2C0: PB11=SCL, PB12=SDA (4.7K pullups required)
 *
 * The Dabao SDK i2c_init(instance, speed_hz) is compatible with
 * I2cInstance enum, so no macro redirect is needed.
 */

#ifndef ROVARI_I2C_H
#define ROVARI_I2C_H

#include "rovari_defs.h"
#include "hardware/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t i2c_write_reg(I2cInstance inst, uint8_t addr, uint8_t reg, uint8_t data);
uint8_t i2c_read_reg(I2cInstance inst, uint8_t addr, uint8_t reg);
int i2c_write_buf(I2cInstance inst, uint8_t addr, const uint8_t *data, uint32_t len);
int i2c_read_buf(I2cInstance inst, uint8_t addr, uint8_t *data, uint32_t len);
uint8_t i2c_write_raw(I2cInstance inst, uint8_t addr, const uint8_t *data, uint16_t len);
void i2c_scan(I2cInstance inst);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class I2c {
public:
    I2c(I2cInstance inst, uint32_t speed = 100000) : _inst(inst) {
        i2c_init((uint)inst, speed);
    }

    uint8_t writeReg(uint8_t addr, uint8_t reg, uint8_t data) {
        return i2c_write_reg(_inst, addr, reg, data);
    }
    uint8_t readReg(uint8_t addr, uint8_t reg) {
        return i2c_read_reg(_inst, addr, reg);
    }
    void scan() { i2c_scan(_inst); }

private:
    I2cInstance _inst;
};

#endif

#endif /* ROVARI_I2C_H */