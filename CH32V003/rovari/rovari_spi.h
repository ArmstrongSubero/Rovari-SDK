/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 * rovari_spi.h - SPI abstraction for CH32V003 (SPI1 only)
 * Default pins: SCK=PC5, MOSI=PC6, MISO=PC7
 */
#ifndef ROVARI_SPI_H
#define ROVARI_SPI_H
#include "rovari_defs.h"
#ifdef __cplusplus
extern "C" {
#endif
void spi_init(SpiInstance inst, uint32_t speed_hz, uint8_t mode, uint8_t lsb_first);
uint8_t spi_transfer(SpiInstance inst, uint8_t tx_byte);
void spi_transfer_buf(SpiInstance inst, uint8_t* buf, uint16_t len);
void spi_write(SpiInstance inst, uint8_t byte);
void spi_write_buf(SpiInstance inst, const uint8_t* buf, uint16_t len);
uint8_t spi_read(SpiInstance inst);
void spi_read_buf(SpiInstance inst, uint8_t* buf, uint16_t len);
#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
class Spi {
public:
    Spi(SpiInstance inst, uint32_t speed, uint8_t mode = 0, uint8_t lsb = 0)
        : _inst(inst) { spi_init(inst, speed, mode, lsb); }
    uint8_t transfer(uint8_t b) { return spi_transfer(_inst, b); }
    void write(uint8_t b) { spi_write(_inst, b); }
    uint8_t read() { return spi_read(_inst); }
private:
    SpiInstance _inst;
};
#endif
#endif
