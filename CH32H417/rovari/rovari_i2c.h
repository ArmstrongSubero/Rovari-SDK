/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_i2c.h - Hardware I2C master abstraction (CH32H417)
 *
 * The CH32H417 has four I2C peripherals. I2C_1 is the recommended
 * instance for 3.3V devices:
 *
 *   I2C_1: SCL=PB6, SDA=PB7  (AF4, VDDIO 3.3V domain)
 *   I2C_2: SCL=PC0, SDA=PC1  (AF9, VIO18 1.8V domain)
 *   I2C_4: SCL=PD12, SDA=PD13 (AF4, VIO18 1.8V domain)
 *
 * I2C_3 is NOT usable (PA8 = TOUCH_RST, PA13/PA14 = SWD debug)
 * and silently maps to I2C_1.
 *
 * Usage:
 *   i2c_init(I2C_1, 100000);
 *   uint8_t count = i2c_scan(I2C_1, addrs, 16);
 *   i2c_write_reg(I2C_1, 0x68, 0x6B, 0x00);
 *   uint8_t who = i2c_read_reg(I2C_1, 0x68, 0x75);
 */

#ifndef ROVARI_I2C_H
#define ROVARI_I2C_H

#include "rovari_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

void i2c_init(I2cInstance inst, uint32_t speed_hz);
uint8_t i2c_scan(I2cInstance inst, uint8_t* out_addrs, uint8_t max_addrs);
uint8_t i2c_write_reg(I2cInstance inst, uint8_t addr, uint8_t reg, uint8_t value);
uint8_t i2c_write_buf(I2cInstance inst, uint8_t addr, uint8_t reg,
                       const uint8_t* data, uint16_t len);
uint8_t i2c_read_reg(I2cInstance inst, uint8_t addr, uint8_t reg);
uint8_t i2c_read_buf(I2cInstance inst, uint8_t addr, uint8_t reg,
                      uint8_t* buf, uint16_t len);
uint8_t i2c_write_raw(I2cInstance inst, uint8_t addr,
                       const uint8_t* data, uint16_t len);
uint8_t i2c_read_raw(I2cInstance inst, uint8_t addr,
                      uint8_t* buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class I2c {
public:
    explicit I2c(I2cInstance inst) : _inst(inst) {}

    void begin(uint32_t speed_hz = 100000) { i2c_init(_inst, speed_hz); }
    uint8_t scan(uint8_t* addrs, uint8_t max) { return i2c_scan(_inst, addrs, max); }

    uint8_t writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
        return i2c_write_reg(_inst, addr, reg, val);
    }
    uint8_t writeBuf(uint8_t addr, uint8_t reg, const uint8_t* data, uint16_t len) {
        return i2c_write_buf(_inst, addr, reg, data, len);
    }
    uint8_t readReg(uint8_t addr, uint8_t reg) {
        return i2c_read_reg(_inst, addr, reg);
    }
    uint8_t readBuf(uint8_t addr, uint8_t reg, uint8_t* buf, uint16_t len) {
        return i2c_read_buf(_inst, addr, reg, buf, len);
    }
    uint8_t writeRaw(uint8_t addr, const uint8_t* data, uint16_t len) {
        return i2c_write_raw(_inst, addr, data, len);
    }
    uint8_t readRaw(uint8_t addr, uint8_t* buf, uint16_t len) {
        return i2c_read_raw(_inst, addr, buf, len);
    }

    I2cInstance instance() const { return _inst; }

private:
    I2cInstance _inst;
};

#endif /* __cplusplus */

#endif /* ROVARI_I2C_H */
