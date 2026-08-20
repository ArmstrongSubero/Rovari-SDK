/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_dma.h - DMA (Direct Memory Access) controller
 *
 * The CH32V307 has two DMA controllers:
 *   DMA1: 7 channels  (general purpose, peripheral-to-memory, memory-to-memory)
 *   DMA2: 11 channels (includes channels for ADC3, SDIO, TIM5, etc.)
 *
 * Each DMA channel can transfer data between:
 *   - Peripheral -> Memory  (e.g., ADC result -> buffer)
 *   - Memory -> Peripheral  (e.g., buffer -> DAC data register)
 *   - Memory -> Memory      (fast block copy/fill)
 *
 * DMA1 Channel Assignments (fixed by hardware):
 *   Ch1: ADC1, TIM2_CH3, TIM4_CH1
 *   Ch2: SPI1_RX, USART3_TX, TIM1_CH1, TIM2_UP, TIM3_CH3
 *   Ch3: SPI1_TX, USART3_RX, TIM1_CH2, TIM3_CH4/UP
 *   Ch4: SPI2_RX, USART1_TX, I2C2_TX, TIM1_CH4/TRIG/COM, TIM4_CH2
 *   Ch5: SPI2_TX, USART1_RX, I2C2_RX, TIM1_UP, TIM2_CH1, TIM4_CH3
 *   Ch6: USART2_RX, I2C1_TX, TIM1_CH3, TIM3_CH1/TRIG
 *   Ch7: USART2_TX, I2C1_RX, TIM2_CH2/CH4, TIM4_UP
 *
 * Usage:
 *   // Memory-to-memory copy (like a fast memcpy)
 *   dma_memcpy(dst, src, count);
 *
 *   // ADC with DMA (continuous multi-channel scanning)
 *   uint16_t buf[4];
 *   dma_adc_init(channels, 4, buf);
 *   dma_adc_start();
 *   // buf[] is updated automatically by DMA
 *
 *   // DAC waveform output via DMA
 *   dma_dac_init(PA4, wave, 256, freq_hz);
 *   dma_dac_start(PA4);
 *
 *   // SPI bulk write via DMA (for displays, flash, etc.)
 *   dma_spi_tx_init(SPI_2);
 *   dma_spi_tx_start(SPI_2, buf, 4800);
 *   dma_spi_tx_wait(SPI_2);
 */

#ifndef ROVARI_DMA_H
#define ROVARI_DMA_H

#include "rovari_defs.h"

/* ===================================================================
 *  C API
 * =================================================================== */
#ifdef __cplusplus
extern "C" {
#endif

/* Memory-to-Memory DMA */

/**
 * Copy a block of 32-bit words from src to dst using DMA.
 * This is a blocking call; it returns when the transfer is complete.
 * Uses DMA2_Channel5 in memory-to-memory mode.
 *
 * Faster than memcpy() for large blocks because the CPU is free
 * during the transfer (though this wrapper blocks for simplicity).
 *
 *   uint32_t src[256], dst[256];
 *   dma_memcpy(dst, src, 256);   // Copy 256 words (1024 bytes)
 *
 * @param dst    Destination buffer (must be word-aligned)
 * @param src    Source buffer (must be word-aligned)
 * @param count  Number of 32-bit words to copy
 */
void dma_memcpy(volatile uint32_t* dst, const volatile uint32_t* src, uint16_t count);

/**
 * Non-blocking memory-to-memory DMA copy.
 * Starts the transfer and returns immediately.
 * Call dma_memcpy_busy() to check if the transfer is still in progress.
 * Call dma_memcpy_wait() to block until complete.
 *
 * @param dst    Destination buffer (word-aligned)
 * @param src    Source buffer (word-aligned)
 * @param count  Number of 32-bit words to copy
 */
void dma_memcpy_start(volatile uint32_t* dst, const volatile uint32_t* src, uint16_t count);

/**
 * Check if a non-blocking DMA memcpy is still in progress.
 * @return  1 if DMA is still transferring, 0 if complete
 */
uint8_t dma_memcpy_busy(void);

/**
 * Block until a non-blocking DMA memcpy completes.
 */
void dma_memcpy_wait(void);

/* ADC with DMA (continuous scan) */

/**
 * Initialize continuous ADC scanning with DMA.
 *
 * Configures ADC1 in scan + continuous mode. DMA1_Channel1 transfers
 * results directly into your buffer in circular mode, so the buffer is
 * always up-to-date without any CPU intervention.
 *
 *   pin_t channels[] = { PA0, PA1, PA2, PA3 };
 *   uint16_t buf[4];
 *   dma_adc_init(channels, 4, buf);
 *   dma_adc_start();
 *   // Now buf[0]=PA0, buf[1]=PA1, etc., updated continuously
 *
 * @param pins       Array of ADC-capable pins (PA0-PA7, PB0-PB1, PC0-PC5)
 * @param num_pins   Number of pins (1-16)
 * @param buffer     Destination buffer; must hold at least num_pins elements
 */
void dma_adc_init(const pin_t* pins, uint8_t num_pins, volatile uint16_t* buffer);

/**
 * Start continuous ADC+DMA scanning.
 * After this call, the buffer passed to dma_adc_init() is updated
 * automatically. Just read from the buffer whenever you need values.
 */
void dma_adc_start(void);

/**
 * Stop ADC+DMA scanning.
 * The buffer retains the last values.
 */
void dma_adc_stop(void);

/* DAC waveform via DMA */

/**
 * Initialize DMA-driven DAC waveform output.
 *
 * Configures a timer to trigger the DAC at the specified frequency,
 * and DMA to feed samples from the waveform buffer in circular mode.
 * The result is a continuous, jitter-free analog waveform with zero
 * CPU overhead.
 *
 *   // Generate a 1 kHz sine wave (256 samples per cycle)
 *   uint16_t sine[256];
 *   // ... fill sine[] with 12-bit values (0-4095)
 *   dma_dac_init(PA4, sine, 256, 256000);  // 256 samples x 1000 Hz = 256 kHz sample rate
 *
 * @param pin         PA4 (DAC channel 1) or PA5 (DAC channel 2)
 * @param waveform    Buffer of 12-bit DAC values (0-4095)
 * @param length      Number of samples in the waveform buffer
 * @param sample_rate Samples per second (determines output frequency)
 *                    Output freq = sample_rate / length
 */
void dma_dac_init(pin_t pin, const uint16_t* waveform, uint16_t length, uint32_t sample_rate);

/**
 * Start DAC+DMA waveform output.
 * The DAC continuously outputs the waveform with zero CPU load.
 */
void dma_dac_start(pin_t pin);

/**
 * Stop DAC+DMA waveform output.
 * The DAC output holds at whatever value was last written.
 */
void dma_dac_stop(pin_t pin);

/**
 * Update the sample rate of a running DAC+DMA waveform.
 * Changes take effect on the next timer period.
 *
 * @param pin         PA4 or PA5
 * @param sample_rate New samples per second
 */
void dma_dac_set_rate(pin_t pin, uint32_t sample_rate);

/* SPI TX via DMA */

/*
 * DMA channel assignments for SPI (hardware-fixed):
 *   SPI1_TX -> DMA1_Channel3
 *   SPI1_RX -> DMA1_Channel2
 *   SPI2_TX -> DMA1_Channel5
 *   SPI2_RX -> DMA1_Channel4
 *   SPI3 has no DMA support on CH32V307
 *
 * Typical use for display drivers, SPI flash bulk writes, etc.:
 *
 *   spi_init(SPI_2, 36000000, 0, 0);   // init SPI2 at 36 MHz
 *   dma_spi_tx_init(SPI_2);            // enable DMA on SPI2 TX
 *
 *   pin_write(cs, Low);
 *   dma_spi_tx_start(SPI_2, buf, len);
 *   dma_spi_tx_wait(SPI_2);
 *   pin_write(cs, High);
 *
 * For transfers larger than 65535 bytes, call dma_spi_tx_start/wait
 * in a loop with chunks <= 65534 bytes each.
 */

/**
 * Initialize DMA for SPI TX (write-only, memory -> SPI data register).
 * Call this once after spi_init(). Enables the SPI DMA TX request
 * and configures the appropriate DMA channel.
 *
 * Supports SPI_1 and SPI_2 only (SPI_3 has no DMA on CH32V307).
 *
 * @param inst  SPI_1 or SPI_2
 */
void dma_spi_tx_init(SpiInstance inst);

/**
 * Start a non-blocking DMA transfer from memory to SPI.
 * Returns immediately. Call dma_spi_tx_wait() to block until done,
 * or dma_spi_tx_busy() to poll.
 *
 * @param inst  SPI_1 or SPI_2
 * @param data  Source buffer (byte array)
 * @param len   Number of bytes to send (1-65534)
 */
void dma_spi_tx_start(SpiInstance inst, const uint8_t* data, uint16_t len);

/**
 * Check if a SPI DMA TX transfer is still in progress.
 * @param inst  SPI_1 or SPI_2
 * @return  1 if busy, 0 if complete
 */
uint8_t dma_spi_tx_busy(SpiInstance inst);

/**
 * Block until a SPI DMA TX transfer completes.
 * Also waits for the SPI shift register to finish (BSY flag clear).
 * @param inst  SPI_1 or SPI_2
 */
void dma_spi_tx_wait(SpiInstance inst);

#ifdef __cplusplus
}
#endif

/* ===================================================================
 *  C++ API
 * =================================================================== */
#ifdef __cplusplus

/**
 * DmaAdc provides continuous multi-channel ADC scanning via DMA.
 *
 * Usage:
 *   pin_t ch[] = { PA0, PA1 };
 *   uint16_t buf[2];
 *   DmaAdc scanner(ch, 2, buf);
 *   scanner.begin();
 *   uint16_t pa0_val = buf[0];
 */
class DmaAdc {
public:
    DmaAdc(const pin_t* pins, uint8_t num, volatile uint16_t* buf)
        : _pins(pins), _num(num), _buf(buf) {}

    void begin()        { dma_adc_init(_pins, _num, _buf); dma_adc_start(); }
    void stop()         { dma_adc_stop(); }

    /** Read the latest DMA-buffered value for channel index i. */
    uint16_t read(uint8_t i) const { return (i < _num) ? _buf[i] : 0; }

    /** Convert channel index i to a voltage in millivolts (0-3300). */
    uint16_t readMv(uint8_t i) const {
        return (i < _num)
            ? (uint16_t)(((uint32_t)_buf[i] * 3300U) / 4095U)
            : 0U;
    }

private:
    const pin_t*       _pins;
    uint8_t            _num;
    volatile uint16_t* _buf;
};

/**
 * DmaDac provides continuous waveform output via DMA and DAC.
 *
 * Usage:
 *   uint16_t wave[256];
 *   // fill wave...
 *   DmaDac gen(PA4, wave, 256, 256000);
 *   gen.begin();
 */
class DmaDac {
public:
    DmaDac(pin_t pin, const uint16_t* wave, uint16_t len, uint32_t rate)
        : _pin(pin), _wave(wave), _len(len), _rate(rate) {}

    void begin()                   { dma_dac_init(_pin, _wave, _len, _rate); dma_dac_start(_pin); }
    void stop()                    { dma_dac_stop(_pin); }
    void setRate(uint32_t rate)    { _rate = rate; dma_dac_set_rate(_pin, rate); }

    pin_t pin() const              { return _pin; }

private:
    pin_t           _pin;
    const uint16_t* _wave;
    uint16_t        _len;
    uint32_t        _rate;
};

/**
 * DmaSpiTx provides bulk SPI writes via DMA.
 *
 * Usage:
 *   Spi spi(SPI_2, 36000000);
 *   DmaSpiTx dma(SPI_2);
 *
 *   pin_write(cs, Low);
 *   dma.send(buf, len);   // blocking
 *   pin_write(cs, High);
 *
 *   // Or non-blocking:
 *   dma.start(buf, len);
 *   // ... do other work ...
 *   dma.wait();
 */
class DmaSpiTx {
public:
    DmaSpiTx(SpiInstance inst) : _inst(inst) {
        dma_spi_tx_init(inst);
    }

    /** Start a non-blocking DMA transfer. */
    void start(const uint8_t* data, uint16_t len) {
        dma_spi_tx_start(_inst, data, len);
    }

    /** Check if transfer is in progress. */
    bool busy() const { return dma_spi_tx_busy(_inst) != 0; }

    /** Block until transfer completes. */
    void wait() { dma_spi_tx_wait(_inst); }

    /** Blocking send: start + wait. */
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