/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 * rovari_i2c.h - I2C abstraction for CH32V003 (I2C1 only)
 * Default pins: SCL=PC2, SDA=PC1
 */
#ifndef ROVARI_I2C_H
#define ROVARI_I2C_H
#include "rovari_defs.h"
#ifdef __cplusplus
extern "C" {
#endif
void i2c_init(I2cInstance inst, uint32_t speed_hz);
uint8_t i2c_write_reg(I2cInstance inst, uint8_t addr, uint8_t reg, uint8_t data);
uint8_t i2c_read_reg(I2cInstance inst, uint8_t addr, uint8_t reg);
uint8_t i2c_write_buf(I2cInstance inst, uint8_t addr, uint8_t reg, const uint8_t* data, uint16_t len);
uint8_t i2c_read_buf(I2cInstance inst, uint8_t addr, uint8_t reg, uint8_t* data, uint16_t len);
uint8_t i2c_write_raw(I2cInstance inst, uint8_t addr, const uint8_t* data, uint16_t len);
void i2c_scan(I2cInstance inst);
#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
class I2c {
public:
    I2c(I2cInstance inst, uint32_t speed = 100000) : _inst(inst) { i2c_init(inst, speed); }
    uint8_t writeReg(uint8_t addr, uint8_t reg, uint8_t data) { return i2c_write_reg(_inst, addr, reg, data); }
    uint8_t readReg(uint8_t addr, uint8_t reg) { return i2c_read_reg(_inst, addr, reg); }
    void scan() { i2c_scan(_inst); }
private:
    I2cInstance _inst;
};
#endif
#endif
