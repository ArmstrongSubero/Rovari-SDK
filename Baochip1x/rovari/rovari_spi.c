/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari - rvembedded.com
 */

/**
 * @file rovari_spi.c
 * @brief SPI wrapper for Baochip-1x.
 *
 * Maps the Rovari pin-friendly API to the Dabao SDK UDMA SPI.
 * Stores clock divider per instance so single-byte calls are fast.
 *
 * NOTE: Does NOT include rovari_spi.h to avoid the spi_init
 * macro capturing calls to the Dabao SDK.
 */

#include <stdint.h>
#include "hardware/spi.h"
#include "rovari_defs.h"

#define SPI_PERCLK_HZ  100000000U
#define MAX_INSTANCES   4

static uint8_t s_clkdiv[MAX_INSTANCES] = {49, 49, 49, 49};

void _rovari_spi_init(SpiInstance inst, uint32_t clock_hz,
                      uint8_t cpol, uint8_t cpha)
{
    (void)cpol;
    (void)cpha;

    uint32_t idx = (uint32_t)inst;
    if (idx >= MAX_INSTANCES) return;

    /* clkdiv = perclk / (2 * clock_hz) - 1 */
    uint32_t div = SPI_PERCLK_HZ / (2 * clock_hz);
    if (div > 0) div--;
    if (div > 255) div = 255;
    s_clkdiv[idx] = (uint8_t)div;

    spi_init(idx);
}

void _rovari_spi_write(SpiInstance inst, uint8_t data)
{
    uint32_t idx = (uint32_t)inst;
    if (idx >= MAX_INSTANCES) return;

    spi_write_blocking(idx, &data, 1, s_clkdiv[idx]);
}

void _rovari_spi_write_buf(SpiInstance inst, const uint8_t *data, uint32_t len)
{
    uint32_t idx = (uint32_t)inst;
    if (idx >= MAX_INSTANCES) return;

    spi_write_blocking(idx, data, len, s_clkdiv[idx]);
}

uint8_t _rovari_spi_read(SpiInstance inst)
{
    uint32_t idx = (uint32_t)inst;
    if (idx >= MAX_INSTANCES) return 0;

    uint8_t tx = 0x00;
    uint8_t rx = 0;
    spi_write_read_blocking(idx, &tx, &rx, 1, s_clkdiv[idx]);
    return rx;
}

uint8_t _rovari_spi_transfer(SpiInstance inst, uint8_t data)
{
    uint32_t idx = (uint32_t)inst;
    if (idx >= MAX_INSTANCES) return 0;

    uint8_t rx = 0;
    spi_write_read_blocking(idx, &data, &rx, 1, s_clkdiv[idx]);
    return rx;
}