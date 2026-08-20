/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_spi.h — SPI master abstraction (C functions + C++ Spi class)
 *
 * SPI is always full-duplex: every transfer simultaneously sends and
 * receives one byte.  The three core operations are:
 *   transfer(tx)        — send tx, return rx
 *   write(byte)         — send byte, discard rx
 *   read()              — send 0xFF dummy, return rx
 *
 * Chip select (CS) is managed manually via a Gpio pin.  This is
 * standard practice — hardware NSS is rarely used because most
 * designs have multiple SPI devices sharing the bus.
 */

#ifndef ROVARI_SPI_H
#define ROVARI_SPI_H

#include "rovari_defs.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  C API — works in both .c and .rova files
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize an SPI peripheral in master mode.
 * Auto-enables clocks and configures SCK/MOSI/MISO pins.
 * CS is not configured here — manage it yourself with a Gpio pin.
 *
 *   spi_init(SPI_1, 1000000, 0, 0);  // 1 MHz, mode 0, MSB first
 *
 * @param inst      SPI_1 or SPI_2
 * @param speed_hz  Desired clock speed in Hz (will round down to nearest prescaler)
 * @param mode      SPI mode 0–3 (CPOL | CPHA)
 * @param lsb_first 0 = MSB first (default), 1 = LSB first
 */
void spi_init(SpiInstance inst, uint32_t speed_hz, uint8_t mode, uint8_t lsb_first);

/**
 * Transfer one byte (full-duplex). Sends tx_byte, returns received byte.
 */
uint8_t spi_transfer(SpiInstance inst, uint8_t tx_byte);

/**
 * Transfer a buffer in-place. Each byte in buf is sent and replaced
 * with the received byte.
 */
void spi_transfer_buf(SpiInstance inst, uint8_t* buf, uint16_t len);

/**
 * Write one byte, discarding the received byte.
 */
void spi_write(SpiInstance inst, uint8_t byte);

/**
 * Write a buffer of bytes, discarding all received bytes.
 */
void spi_write_buf(SpiInstance inst, const uint8_t* buf, uint16_t len);

/**
 * Read one byte by sending 0xFF as a dummy. Returns received byte.
 */
uint8_t spi_read(SpiInstance inst);

/**
 * Read len bytes into buf by sending 0xFF dummies.
 */
void spi_read_buf(SpiInstance inst, uint8_t* buf, uint16_t len);

#ifdef __cplusplus
}
#endif

/* ═══════════════════════════════════════════════════════════════════════
 *  C++ API — available in .rova files (compiled as C++)
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef __cplusplus

class Spi {
public:
    /**
     * Construct SPI in master mode. Hardware init is deferred until
     * begin() or first transfer, so global declarations are safe:
     *   Spi spi(SPI_1);                   // 1 MHz, mode 0, MSB first
     *   Spi spi(SPI_1, 8000000);          // 8 MHz, mode 0, MSB first
     *   Spi spi(SPI_1, 4000000, 0);       // 4 MHz, mode 0, MSB first
     *   Spi spi(SPI_1, 1000000, 3, true); // 1 MHz, mode 3, LSB first
     */
    Spi(SpiInstance inst, uint32_t speed_hz = 1000000,
        uint8_t mode = 0, bool lsb_first = false)
        : _inst(inst), _speed(speed_hz), _mode(mode),
          _lsb(lsb_first ? (uint8_t)1 : (uint8_t)0), _inited(false)
    {}

    /** Explicitly initialize the hardware */
    void begin() {
        if (!_inited) {
            spi_init(_inst, _speed, _mode, _lsb);
            _inited = true;
        }
    }

    /** Full-duplex transfer: send tx, return rx */
    uint8_t transfer(uint8_t tx) {
        _ensure_init();
        return spi_transfer(_inst, tx);
    }

    /** Full-duplex transfer of a buffer in-place */
    void transfer(uint8_t* buf, uint16_t len) {
        _ensure_init();
        spi_transfer_buf(_inst, buf, len);
    }

    /** Write one byte, discard rx */
    void write(uint8_t byte) {
        _ensure_init();
        spi_write(_inst, byte);
    }

    /** Write a buffer, discard rx */
    void write(const uint8_t* buf, uint16_t len) {
        _ensure_init();
        spi_write_buf(_inst, buf, len);
    }

    /** Read one byte (sends 0xFF dummy) */
    uint8_t read() {
        _ensure_init();
        return spi_read(_inst);
    }

    /** Read len bytes into buf (sends 0xFF dummies) */
    void read(uint8_t* buf, uint16_t len) {
        _ensure_init();
        spi_read_buf(_inst, buf, len);
    }

    /** Get the instance */
    SpiInstance instance() const { return _inst; }

private:
    SpiInstance _inst;
    uint32_t   _speed;
    uint8_t    _mode;
    uint8_t    _lsb;
    bool       _inited;

    void _ensure_init() {
        if (!_inited) {
            spi_init(_inst, _speed, _mode, _lsb);
            _inited = true;
        }
    }
};

#endif /* __cplusplus */

#endif /* ROVARI_SPI_H */