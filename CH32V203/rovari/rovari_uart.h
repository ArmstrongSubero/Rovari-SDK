/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_uart.h — UART abstraction (C functions + C++ Uart class)
 */

#ifndef ROVARI_UART_H
#define ROVARI_UART_H

#include "rovari_defs.h"
#include <stdarg.h>

/* ═══════════════════════════════════════════════════════════════════════
 *  C API — works in both .c and .rova files
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize a UART peripheral. Auto-enables clocks and configures TX/RX pins.
 *   uart_init(SERIAL1, 115200);
 */
void uart_init(UartInstance inst, uint32_t baud);

/**
 * Send a single byte.
 */
void uart_write_byte(UartInstance inst, uint8_t byte);

/**
 * Send a null-terminated string.
 *   uart_print(SERIAL1, "Hello");
 */
void uart_print(UartInstance inst, const char* str);

/**
 * Send a null-terminated string followed by "\r\n".
 *   uart_println(SERIAL1, "Hello");
 */
void uart_println(UartInstance inst, const char* str);

/**
 * Formatted print (like printf).
 *   uart_printf(SERIAL1, "Value: %d\n", 42);
 */
void uart_printf(UartInstance inst, const char* fmt, ...);

/**
 * Read a single byte (blocking). Returns the byte received.
 */
uint8_t uart_read_byte(UartInstance inst);

/**
 * Check if data is available to read. Returns 1 if data ready, 0 otherwise.
 */
uint8_t uart_available(UartInstance inst);

/**
 * Read bytes until '\n' or buffer is full. Returns number of bytes read.
 * Non-blocking: returns 0 immediately if no complete line is available.
 * The line terminator is stripped from the output.
 *   char buf[64];
 *   int n = uart_read_line(SERIAL1, buf, sizeof(buf));
 */
int uart_read_line(UartInstance inst, char* buf, int max_len);

#ifdef __cplusplus
}
#endif

/* ═══════════════════════════════════════════════════════════════════════
 *  C++ API — available in .rova files (compiled as C++)
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef __cplusplus

class Uart {
public:
    /**
     * Default constructor — creates an uninitialized Uart on SERIAL1.
     * Call begin() before use.
     *   Uart serial;             // not yet initialized
     *   serial.begin(115200);    // now ready
     */
    Uart() : _inst(SERIAL1), _baud(0), _initialized(false) {}

    /**
     * Construct with explicit instance, but defer initialization.
     *   Uart serial2(SERIAL2);
     *   serial2.begin(9600);
     */
    Uart(UartInstance inst) : _inst(inst), _baud(0), _initialized(false) {}

    /**
     * Construct with instance and baud rate. Hardware init is deferred
     * until begin() or first print/write, so global declarations are safe:
     *   Uart serial2(SERIAL2, 115200);   // safe as global
     */
    Uart(UartInstance inst, uint32_t baud) : _inst(inst), _baud(baud), _initialized(false) {}

    /**
     * Initialize (or re-initialize) the UART at the given baud rate.
     *   serial.begin(115200);
     */
    void begin(uint32_t baud) {
        uart_init(_inst, baud);
        _initialized = true;
    }

    /**
     * Initialize with explicit instance and baud rate.
     *   serial.begin(SERIAL2, 9600);
     */
    void begin(UartInstance inst, uint32_t baud) {
        _inst = inst;
        uart_init(_inst, baud);
        _initialized = true;
    }

    /** Send a null-terminated string */
    void print(const char* str) {
        _ensure_init();
        uart_print(_inst, str);
    }

    /** Send a string followed by \r\n */
    void println(const char* str) {
        _ensure_init();
        uart_println(_inst, str);
    }

    /** Print an empty line */
    void println() {
        _ensure_init();
        uart_print(_inst, "\r\n");
    }

    /** Formatted print */
    void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        _ensure_init();
        va_list args;
        va_start(args, fmt);
        char buf[256];
        extern int vsnprintf(char*, size_t, const char*, va_list);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        uart_print(_inst, buf);
    }

    /** Send a single byte */
    void write(uint8_t byte) {
        _ensure_init();
        uart_write_byte(_inst, byte);
    }

    /** Send a buffer of bytes */
    void write(const uint8_t* data, uint16_t len) {
        _ensure_init();
        for (uint16_t i = 0; i < len; i++) {
            uart_write_byte(_inst, data[i]);
        }
    }

    /** Read a single byte (blocking) */
    uint8_t read() {
        _ensure_init();
        return uart_read_byte(_inst);
    }

    /** Check if data is available */
    bool available() {
        _ensure_init();
        return uart_available(_inst) != 0;
    }

    /**
     * Read a complete line (non-blocking).
     * Returns the number of characters read, or 0 if no complete line.
     * The newline character is stripped.
     */
    int readLine(char* buf, int maxLen) {
        _ensure_init();
        return uart_read_line(_inst, buf, maxLen);
    }

    /** Get the instance identifier */
    UartInstance instance() const { return _inst; }

    /** Check if begin() has been called */
    bool isReady() const { return _initialized; }

private:
    UartInstance _inst;
    uint32_t    _baud;
    bool        _initialized;

    void _ensure_init() {
        if (!_initialized && _baud > 0) {
            uart_init(_inst, _baud);
            _initialized = true;
        }
    }
};

/* ── Pre-instantiated global for SERIAL1 ────────────────────────────── */
/*  Usage: serial.begin(115200);  serial.println("Hello");              */
/*  C++17 inline variable — no separate .cpp definition file needed.    */
inline Uart serial;

#endif /* __cplusplus */

#endif /* ROVARI_UART_H */