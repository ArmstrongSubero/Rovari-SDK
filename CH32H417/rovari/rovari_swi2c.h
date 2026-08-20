/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_swi2c.h - Software (bit-bang) I2C master driver
 *
 * Pin-configurable software I2C that works on ANY GPIO pin.
 * Ideal for mixed voltage domains (H417 VIO18/VDDIO) where
 * hardware I2C pins may be on the wrong voltage domain.
 *
 * Supports multiple independent buses via the SoftI2c struct.
 * Each bus has its own SCL/SDA pin pair.
 *
 * Usage:
 *   SoftI2c bus;
 *   swi2c_init(&bus, GPIOA, GPIO_Pin_2, GPIOA, GPIO_Pin_4); // SCL=PA2, SDA=PA4
 *
 *   uint8_t id;
 *   swi2c_read_reg(&bus, 0x38, 0xA3, &id, 1);  // Read FT6336U chip ID
 *
 *   swi2c_write_reg(&bus, 0x50, 0x00, data, 4); // Write to EEPROM
 */

#ifndef ROVARI_SWI2C_H
#define ROVARI_SWI2C_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward-declare vendor types to avoid pulling in full HAL header */
#ifndef GPIO_TypeDef
#include "debug.h"
#endif

/* -- Software I2C bus descriptor ----------------------------------- */
typedef struct {
    GPIO_TypeDef* scl_port;
    uint16_t      scl_pin;
    GPIO_TypeDef* sda_port;
    uint16_t      sda_pin;
    uint32_t      delay_count;   /* loop iterations for half-period */
} SoftI2c;

/* -- API ----------------------------------------------------------- */

/**
 * Initialize a software I2C bus.
 * Configures SCL and SDA as open-drain outputs, both high (idle).
 * Enables the GPIO port clocks automatically.
 *
 * Default speed is approximately 100-200 kHz depending on core clock.
 *
 * @param bus       Pointer to a SoftI2c struct (caller allocates)
 * @param scl_port  GPIO port for SCL (e.g. GPIOA)
 * @param scl_pin   GPIO pin mask for SCL (e.g. GPIO_Pin_2)
 * @param sda_port  GPIO port for SDA (e.g. GPIOA)
 * @param sda_pin   GPIO pin mask for SDA (e.g. GPIO_Pin_4)
 */
void swi2c_init(SoftI2c* bus,
                GPIO_TypeDef* scl_port, uint16_t scl_pin,
                GPIO_TypeDef* sda_port, uint16_t sda_pin);

/**
 * Set the bit-bang delay (controls approximate bus speed).
 * Higher values = slower clock. Default is 60 (roughly 100-200 kHz).
 *
 * @param bus    Pointer to initialized SoftI2c
 * @param count  Loop iterations per half-period (min 10, max 1000)
 */
void swi2c_set_speed(SoftI2c* bus, uint32_t count);

/**
 * Scan the bus for devices that ACK their address.
 * Probes addresses 0x08-0x77 (valid 7-bit range).
 *
 * @param bus        Pointer to initialized SoftI2c
 * @param out_addrs  Buffer to receive found 7-bit addresses
 * @param max_addrs  Maximum entries in out_addrs
 * @return           Number of devices found
 */
uint8_t swi2c_scan(SoftI2c* bus, uint8_t* out_addrs, uint8_t max_addrs);

/**
 * Write to a register address, then write data bytes.
 *   [START] [addr+W] [reg] [data0] [data1] ... [STOP]
 *
 * @param bus   Pointer to initialized SoftI2c
 * @param addr  7-bit device address
 * @param reg   Register address byte
 * @param data  Buffer of bytes to write (can be NULL if len==0)
 * @param len   Number of data bytes
 * @return      true on success (all bytes ACKed), false on NACK/error
 */
bool swi2c_write_reg(SoftI2c* bus, uint8_t addr, uint8_t reg,
                     const uint8_t* data, uint16_t len);

/**
 * Read from a register address (write reg, repeated start, read).
 *   [START] [addr+W] [reg] [rSTART] [addr+R] [data0] ... [NACK] [STOP]
 *
 * @param bus   Pointer to initialized SoftI2c
 * @param addr  7-bit device address
 * @param reg   Register address byte
 * @param buf   Buffer to receive data
 * @param len   Number of bytes to read
 * @return      true on success, false on NACK/error
 */
bool swi2c_read_reg(SoftI2c* bus, uint8_t addr, uint8_t reg,
                    uint8_t* buf, uint16_t len);

/**
 * Write raw bytes (no register address).
 *   [START] [addr+W] [data0] [data1] ... [STOP]
 *
 * @param bus   Pointer to initialized SoftI2c
 * @param addr  7-bit device address
 * @param data  Buffer of bytes to send
 * @param len   Number of bytes
 * @return      true on success, false on NACK
 */
bool swi2c_write_raw(SoftI2c* bus, uint8_t addr,
                     const uint8_t* data, uint16_t len);

/**
 * Read raw bytes (no register address).
 *   [START] [addr+R] [data0] ... [NACK] [STOP]
 *
 * @param bus   Pointer to initialized SoftI2c
 * @param addr  7-bit device address
 * @param buf   Buffer to receive data
 * @param len   Number of bytes to read
 * @return      true on success, false on NACK
 */
bool swi2c_read_raw(SoftI2c* bus, uint8_t addr,
                    uint8_t* buf, uint16_t len);

#ifdef __cplusplus
}
#endif

/* -- C++ convenience class ----------------------------------------- */
#ifdef __cplusplus

class SoftWire {
public:
    SoftWire() {}

    void begin(GPIO_TypeDef* scl_port, uint16_t scl_pin,
               GPIO_TypeDef* sda_port, uint16_t sda_pin) {
        swi2c_init(&_bus, scl_port, scl_pin, sda_port, sda_pin);
    }

    void setSpeed(uint32_t count) { swi2c_set_speed(&_bus, count); }

    uint8_t scan(uint8_t* addrs, uint8_t max) {
        return swi2c_scan(&_bus, addrs, max);
    }

    bool writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
        return swi2c_write_reg(&_bus, addr, reg, &val, 1);
    }

    bool writeBuf(uint8_t addr, uint8_t reg, const uint8_t* data, uint16_t len) {
        return swi2c_write_reg(&_bus, addr, reg, data, len);
    }

    uint8_t readReg(uint8_t addr, uint8_t reg) {
        uint8_t val = 0xFF;
        swi2c_read_reg(&_bus, addr, reg, &val, 1);
        return val;
    }

    bool readBuf(uint8_t addr, uint8_t reg, uint8_t* buf, uint16_t len) {
        return swi2c_read_reg(&_bus, addr, reg, buf, len);
    }

    SoftI2c* handle() { return &_bus; }

private:
    SoftI2c _bus;
};

#endif /* __cplusplus */

#endif /* ROVARI_SWI2C_H */
