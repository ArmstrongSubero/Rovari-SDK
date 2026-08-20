/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari - rvembedded.com
 *
 * rovari_spi.h - SPI abstraction for Baochip-1x
 *
 * Dabao board SPI2: PC0=CLK, PC1=MOSI, PC2=MISO, PC3=CS
 *
 * The Dabao SDK defines spi_init(uint instance) in hardware/spi.h.
 * The Rovari wrapper uses prefixed symbols with macro mapping.
 */

#ifndef ROVARI_SPI_H
#define ROVARI_SPI_H

#include "rovari_defs.h"
#include "hardware/spi.h"

#ifdef __cplusplus
extern "C" {
#endif

void     _rovari_spi_init(SpiInstance inst, uint32_t clock_hz,
                          uint8_t cpol, uint8_t cpha);
void     _rovari_spi_write(SpiInstance inst, uint8_t data);
void     _rovari_spi_write_buf(SpiInstance inst, const uint8_t *data, uint32_t len);
uint8_t  _rovari_spi_read(SpiInstance inst);
uint8_t  _rovari_spi_transfer(SpiInstance inst, uint8_t data);

#define spi_init(inst, clk, cpol, cpha) \
    _rovari_spi_init((inst), (clk), (cpol), (cpha))
#define spi_write(inst, data)         _rovari_spi_write((inst), (data))
#define spi_write_buf(inst, data, len) _rovari_spi_write_buf((inst), (data), (len))
#define spi_read(inst)                _rovari_spi_read((inst))
#define spi_transfer(inst, data)      _rovari_spi_transfer((inst), (data))

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class Spi {
public:
    Spi(SpiInstance inst, uint32_t clock, uint8_t cpol = 0, uint8_t cpha = 0)
        : _inst(inst)
    {
        _rovari_spi_init(inst, clock, cpol, cpha);
    }

    void    write(uint8_t data)                    { _rovari_spi_write(_inst, data); }
    void    writeBuf(const uint8_t *data, uint32_t len) { _rovari_spi_write_buf(_inst, data, len); }
    uint8_t read()                                 { return _rovari_spi_read(_inst); }
    uint8_t transfer(uint8_t data)                 { return _rovari_spi_transfer(_inst, data); }

private:
    SpiInstance _inst;
};

#endif

#endif /* ROVARI_SPI_H */