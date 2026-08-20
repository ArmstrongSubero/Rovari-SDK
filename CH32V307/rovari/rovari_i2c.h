/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_i2c.h - I2C master abstraction
 *
 * The CH32V307 has two I2C peripherals:
 *   I2C_1:     SCL=PB6,  SDA=PB7   (APB1, up to 400 kHz, default)
 *   I2C_2:     SCL=PB10, SDA=PB11  (APB1, up to 400 kHz)
 *   I2C_1_ALT: SCL=PB8,  SDA=PB9   (I2C1 remapped to PB8/PB9)
 *
 * I2C_1_ALT is the same I2C1 peripheral on its remapped pins; use it when
 * your wiring follows the WCH examples (which use PB8/PB9). Don't use I2C_1
 * and I2C_1_ALT at the same time; they're one peripheral.
 *
 * This driver implements master-mode I2C with:
 *   Bus scanning (detect connected devices)
 *   Byte and register read/write
 *   Multi-byte (burst) read/write
 *   Timeout protection (no infinite hangs)
 *
 * Chip select is inherent in I2C: each device has a 7-bit address.
 *
 * Usage:
 *   i2c_init(I2C_1, 100000);               // 100 kHz standard mode
 *   uint8_t count = i2c_scan(I2C_1, addrs, 16);  // Scan for devices
 *
 *   i2c_write_reg(I2C_1, 0x68, 0x6B, 0x00);       // Write register
 *   uint8_t who = i2c_read_reg(I2C_1, 0x68, 0x75); // Read register
 */

#ifndef ROVARI_I2C_H
#define ROVARI_I2C_H

#include "rovari_defs.h"

/* -----------------------------------------------------------------------
 *  C API
 * ----------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize an I2C peripheral in master mode.
 *
 *   i2c_init(I2C_1, 100000);   // 100 kHz (standard mode)
 *   i2c_init(I2C_1, 400000);   // 400 kHz (fast mode)
 *
 * @param inst      I2C_1 or I2C_2
 * @param speed_hz  Clock speed: 100000 (standard) or 400000 (fast)
 */
void i2c_init(I2cInstance inst, uint32_t speed_hz);

/**
 * Scan the I2C bus for devices that respond with ACK.
 * Probes addresses 0x08-0x77 (valid 7-bit range).
 *
 *   uint8_t addrs[16];
 *   uint8_t count = i2c_scan(I2C_1, addrs, 16);
 *   for (int i = 0; i < count; i++)
 *       serial.printf("Found: 0x%02X\n", addrs[i]);
 *
 * @param inst       I2C_1 or I2C_2
 * @param out_addrs  Buffer to receive found addresses
 * @param max_addrs  Maximum number of addresses to store
 * @return           Number of devices found
 */
uint8_t i2c_scan(I2cInstance inst, uint8_t* out_addrs, uint8_t max_addrs);

/**
 * Write one byte to a register on an I2C device.
 *
 *   i2c_write_reg(I2C_1, 0x68, 0x6B, 0x00);  // MPU6050: wake up
 *
 * @param inst   I2C_1 or I2C_2
 * @param addr   7-bit device address (0x00-0x7F)
 * @param reg    Register address to write to
 * @param value  Byte to write
 * @return       0 on success, non-zero on error (NACK or timeout)
 */
uint8_t i2c_write_reg(I2cInstance inst, uint8_t addr, uint8_t reg, uint8_t value);

/**
 * Write multiple bytes starting at a register address.
 *
 * @param inst   I2C_1 or I2C_2
 * @param addr   7-bit device address
 * @param reg    Starting register address
 * @param data   Buffer of bytes to write
 * @param len    Number of bytes to write
 * @return       0 on success, non-zero on error
 */
uint8_t i2c_write_buf(I2cInstance inst, uint8_t addr, uint8_t reg,
                       const uint8_t* data, uint16_t len);

/**
 * Read one byte from a register on an I2C device.
 *
 *   uint8_t who = i2c_read_reg(I2C_1, 0x68, 0x75);  // WHO_AM_I
 *
 * @param inst   I2C_1 or I2C_2
 * @param addr   7-bit device address
 * @param reg    Register address to read from
 * @return       Byte read, or 0xFF on error
 */
uint8_t i2c_read_reg(I2cInstance inst, uint8_t addr, uint8_t reg);

/**
 * Read multiple bytes starting at a register address.
 *
 *   uint8_t buf[6];
 *   i2c_read_buf(I2C_1, 0x68, 0x3B, buf, 6);  // Read accel XYZ
 *
 * @param inst   I2C_1 or I2C_2
 * @param addr   7-bit device address
 * @param reg    Starting register address
 * @param buf    Buffer to receive data
 * @param len    Number of bytes to read
 * @return       0 on success, non-zero on error
 */
uint8_t i2c_read_buf(I2cInstance inst, uint8_t addr, uint8_t reg,
                      uint8_t* buf, uint16_t len);

/**
 * Send raw bytes (no register address) to an I2C device.
 * Used for devices that don't use a register-based protocol.
 *
 * @param inst   I2C_1 or I2C_2
 * @param addr   7-bit device address
 * @param data   Bytes to send
 * @param len    Number of bytes
 * @return       0 on success, non-zero on error
 */
uint8_t i2c_write_raw(I2cInstance inst, uint8_t addr,
                       const uint8_t* data, uint16_t len);

/**
 * Read raw bytes (no register address) from an I2C device.
 *
 * @param inst   I2C_1 or I2C_2
 * @param addr   7-bit device address
 * @param buf    Buffer to receive data
 * @param len    Number of bytes to read
 * @return       0 on success, non-zero on error
 */
uint8_t i2c_read_raw(I2cInstance inst, uint8_t addr,
                      uint8_t* buf, uint16_t len);

#ifdef __cplusplus
}
#endif

/* -----------------------------------------------------------------------
 *  C++ API
 * ----------------------------------------------------------------------- */
#ifdef __cplusplus

/**
 * I2c is the C++ wrapper for the Rovari I2C master API.
 *
 * Usage:
 *   I2c wire(I2C_1);
 *   wire.begin();                              // 100 kHz default
 *   wire.begin(400000);                        // 400 kHz fast mode
 *   uint8_t n = wire.scan(addrs, 16);          // Scan bus
 *   wire.writeReg(0x68, 0x6B, 0x00);          // Write register
 *   uint8_t val = wire.readReg(0x68, 0x75);   // Read register
 */
class I2c {
public:
    explicit I2c(I2cInstance inst) : _inst(inst) {}

    /** Initialize at the given speed (default 100 kHz). */
    void begin(uint32_t speed_hz = 100000) { i2c_init(_inst, speed_hz); }

    /** Scan bus and return number of devices found. */
    uint8_t scan(uint8_t* addrs, uint8_t max) { return i2c_scan(_inst, addrs, max); }

    /** Write one register. Returns 0 on success. */
    uint8_t writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
        return i2c_write_reg(_inst, addr, reg, val);
    }

    /** Write multiple registers. Returns 0 on success. */
    uint8_t writeBuf(uint8_t addr, uint8_t reg, const uint8_t* data, uint16_t len) {
        return i2c_write_buf(_inst, addr, reg, data, len);
    }

    /** Read one register. Returns byte or 0xFF on error. */
    uint8_t readReg(uint8_t addr, uint8_t reg) {
        return i2c_read_reg(_inst, addr, reg);
    }

    /** Read multiple registers. Returns 0 on success. */
    uint8_t readBuf(uint8_t addr, uint8_t reg, uint8_t* buf, uint16_t len) {
        return i2c_read_buf(_inst, addr, reg, buf, len);
    }

    /** Send raw bytes (no register). */
    uint8_t writeRaw(uint8_t addr, const uint8_t* data, uint16_t len) {
        return i2c_write_raw(_inst, addr, data, len);
    }

    /** Read raw bytes (no register). */
    uint8_t readRaw(uint8_t addr, uint8_t* buf, uint16_t len) {
        return i2c_read_raw(_inst, addr, buf, len);
    }

    I2cInstance instance() const { return _inst; }

private:
    I2cInstance _inst;
};

#endif /* __cplusplus */

#endif /* ROVARI_I2C_H */