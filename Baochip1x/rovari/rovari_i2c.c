/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari - rvembedded.com
 */

/**
 * @file rovari_i2c.c
 * @brief I2C register-level helpers for Baochip-1x.
 *
 * Wraps the Dabao SDK i2c_write_blocking / i2c_write_read_blocking
 * to provide single-register read/write and bus scan.
 */

#include <stdint.h>
#include "hardware/i2c.h"
#include "bao/stdlib.h"
#include "rovari_defs.h"

uint8_t i2c_write_reg(I2cInstance inst, uint8_t addr, uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = {reg, data};
    int rc = i2c_write_blocking((uint)inst, addr, buf, 2);
    return (rc == 0) ? 0 : 1;
}

uint8_t i2c_read_reg(I2cInstance inst, uint8_t addr, uint8_t reg)
{
    uint8_t val = 0;
    i2c_write_read_blocking((uint)inst, addr, &reg, 1, &val, 1);
    return val;
}

int i2c_write_buf(I2cInstance inst, uint8_t addr, const uint8_t *data, uint32_t len)
{
    return i2c_write_blocking((uint)inst, addr, data, len);
}

uint8_t i2c_write_raw(I2cInstance inst, uint8_t addr, const uint8_t *data, uint16_t len)
{
    int rc = i2c_write_blocking((uint)inst, addr, data, (uint32_t)len);
    return (rc == 0) ? 0 : 1;
}

int i2c_read_buf(I2cInstance inst, uint8_t addr, uint8_t *data, uint32_t len)
{
    return i2c_read_blocking((uint)inst, addr, data, len);
}

void i2c_scan(I2cInstance inst)
{
    mini_printf("Scanning I2C bus...\r\n");
    uint8_t found = 0;

    for (uint8_t addr = 0x08; addr < 0x78; addr++)
    {
        uint8_t dummy = 0;
        int rc = i2c_read_blocking((uint)inst, addr, &dummy, 1);
        if (rc == 0)
        {
            mini_printf("  Device at 0x%x\r\n", addr);
            found++;
        }
    }

    if (found == 0)
    {
        mini_printf("  No devices found\r\n");
    }
    else
    {
        mini_printf("  %d device(s) found\r\n", found);
    }
}