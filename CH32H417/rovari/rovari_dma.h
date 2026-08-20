/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_dma.h - DMA (Direct Memory Access) for CH32H417
 *
 * The CH32H417 has two DMA controllers (DMA1 + DMA2, 8 channels each)
 * and a DMAMUX that allows flexible routing of any peripheral request
 * to any channel. This differs from the CH32V307 where channels have
 * fixed hardware assignments.
 *
 * Currently implemented:
 *   - SPI TX via DMA (for display driver, SPI flash bulk writes)
 *
 * Usage:
 *   spi_init(SPI_2, 50000000, 0, 0);
 *   dma_spi_tx_init(SPI_2);
 *
 *   pin_write(cs, Low);
 *   dma_spi_tx_start(SPI_2, buf, len);
 *   dma_spi_tx_wait(SPI_2);
 *   pin_write(cs, High);
 *
 * For transfers larger than 65535 bytes, call start/wait in a loop
 * with chunks <= 65534 bytes each.
 */

#ifndef ROVARI_DMA_H
#define ROVARI_DMA_H

#include "rovari_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -- SPI TX via DMA ------------------------------------------------ */

/**
 * Initialize DMA for SPI TX (write-only, memory -> SPI data register).
 * Call this once after spi_init(). Configures DMAMUX routing and
 * enables the SPI DMA TX request.
 *
 * Supports SPI_1 through SPI_4.
 *
 * @param inst  SPI_1, SPI_2, SPI_3, or SPI_4
 */
void dma_spi_tx_init(SpiInstance inst);

/**
 * Start a non-blocking DMA transfer from memory to SPI.
 * Returns immediately. Call dma_spi_tx_wait() to block until done.
 *
 * @param inst  SPI instance
 * @param data  Source buffer (byte array)
 * @param len   Number of bytes to send (1-65534)
 */
void dma_spi_tx_start(SpiInstance inst, const uint8_t* data, uint16_t len);

/**
 * Check if a SPI DMA TX transfer is still in progress.
 * @param inst  SPI instance
 * @return  1 if busy, 0 if complete
 */
uint8_t dma_spi_tx_busy(SpiInstance inst);

/**
 * Block until a SPI DMA TX transfer completes.
 * Also waits for the SPI shift register to finish (BSY flag clear).
 * @param inst  SPI instance
 */
void dma_spi_tx_wait(SpiInstance inst);

/**
 * Start a chained DMA transfer (SPI1 only).
 * Uses DMA TC interrupt to auto-chain 64K chunks with zero gap.
 * Eliminates flicker lines between chunks on LCD push.
 */
void dma_spi_tx_start_chained(SpiInstance inst, const uint8_t* data, uint32_t total_len);
uint8_t dma_spi_tx_chain_busy(void);
void dma_spi_tx_chain_wait(void);

#ifdef __cplusplus
}
#endif

/* -- C++ convenience class ----------------------------------------- */
#ifdef __cplusplus

class DmaSpiTx {
public:
    DmaSpiTx(SpiInstance inst) : _inst(inst) {
        dma_spi_tx_init(inst);
    }

    void start(const uint8_t* data, uint16_t len) {
        dma_spi_tx_start(_inst, data, len);
    }

    bool busy() const { return dma_spi_tx_busy(_inst) != 0; }

    void wait() { dma_spi_tx_wait(_inst); }

    void send(const uint8_t* data, uint16_t len) {
        start(data, len);
        wait();
    }

    SpiInstance instance() const { return _inst; }

private:
    SpiInstance _inst;
};

#endif /* __cplusplus */

#endif /* ROVARI_DMA_H */
