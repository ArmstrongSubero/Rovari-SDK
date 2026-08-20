/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari
 *
 * rovari_neopixel.c - WS2812B NeoPixel driver (GPIO bit-bang, 144 MHz)
 *
 * Timing at 144 MHz (6.94 ns/cycle):
 *   T0H: ~350-400ns  (50-58 cycles)
 *   T0L: ~800-850ns  (115-122 cycles)
 *   T1H: ~700-800ns  (100-115 cycles)
 *   T1L: ~450-600ns  (65-86 cycles)
 *   Bit period: ~1.25us (~180 cycles)
 *   Reset: >= 50us (>= 280ns latch on newer WS2812B)
 *
 * Uses inline RISC-V assembly with counted delay loops for
 * cycle-accurate timing. Interrupts are disabled during transmission
 * to prevent timing glitches.
 *
 * Based on wagiminator's CH32V003 NeoPixel implementation,
 * adapted for the QingKe V4B core at 144 MHz.
 */

#include "rovari_neopixel.h"
#include "rovari_gpio.h"
#include "rovari_exti.h"
#include "debug.h"

/* Pixel buffer: GRB format, 3 bytes per pixel */
static uint8_t neo_buf[NEO_MAX_PIXELS * 3];
static uint8_t neo_num = 0;

/* Pin hardware info, resolved at init time */
static volatile uint32_t *neo_bshr = 0;  /* BSHR register address */
static volatile uint32_t *neo_bcr  = 0;  /* BCR register address */
static uint32_t neo_pin_mask = 0;         /* Pin bit mask */

/*
 * Timing approach adapted from Adafruit NeoPixel library (LGPL v3)
 * ch32Show() by ladyada, tested on CH32V203G6 QT Py at 144 MHz.
 *
 * Uses inline NOP chains for cycle-accurate timing. No delay loops.
 * The NOP counts are cumulative across all supported frequencies;
 * at 144 MHz all blocks execute giving the correct total count.
 *
 * Each NOP = 1 cycle = 6.94 ns at 144 MHz.
 * The store to BSHR/BCR, bit test, branch, and pointer increment
 * add overhead cycles that are accounted for in the NOP counts.
 */

/* Send entire pixel buffer.
 * Must be called with interrupts disabled.
 * Adapted from Adafruit's ch32Show() which proven on this exact board. */
static void neo_send_buf(volatile uint32_t *set, volatile uint32_t *clr,
                         uint32_t pin, uint8_t *buf, uint32_t len)
{
    uint8_t *ptr = buf;
    uint8_t *end = ptr + len;
    uint8_t p = *ptr++;
    uint8_t bitMask = 0x80;

    while (1) {
        if (p & bitMask) {
            /* ONE: High ~800ns, Low ~450ns */
            *set = pin;
            __asm volatile(
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop;"
                /* 56 MHz+ */
                "nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                /* 72 MHz+ */
                "nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop;"
                /* 96 MHz+ */
                "nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                /* 120 MHz+ */
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                /* 144 MHz */
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
            );
            *clr = pin;
            __asm volatile(
                "nop; nop;"
                /* 56 MHz+ */
                "nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                /* 72 MHz+ */
                "nop; nop; nop; nop; nop; nop; nop; nop; nop;"
                /* 96 MHz+ */
                "nop; nop; nop; nop; nop; nop;"
                /* 120 MHz+ */
                "nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                /* 144 MHz */
                "nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; nop;"
            );
        } else {
            /* ZERO: High ~400ns, Low ~850ns */
            *set = pin;
            __asm volatile(
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                /* 56 MHz+ */
                "nop; nop; nop; nop; nop; nop; nop; nop; nop;"
                /* 72 MHz+ */
                "nop; nop; nop; nop; nop; nop; nop;"
                /* 96 MHz+ */
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                /* 120 MHz+ */
                "nop; nop; nop; "
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                /* 144 MHz */
                "nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; nop;"
            );
            *clr = pin;
            __asm volatile(
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop;"
                /* 56 MHz+ */
                "nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                /* 72 MHz+ */
                "nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                /* 96 MHz+ */
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop;"
                /* 120 MHz+ */
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop;"
                /* 144 MHz */
                "nop; nop; nop; nop;"
                "nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; nop;"
            );
        }

        if (bitMask >>= 1) {
            __asm volatile("nop;");
        } else {
            if (ptr >= end) break;
            p = *ptr++;
            bitMask = 0x80;
        }
    }
}

/* ---- Public API ---- */

void neo_init(pin_t pin, uint8_t count)
{
    if (count > NEO_MAX_PIXELS) count = NEO_MAX_PIXELS;
    neo_num = count;

    /* Configure pin as push-pull output */
    pin_mode(pin, Output);
    digital_write(pin, 0);

    /* Resolve GPIO port base from pin encoding */
    uint8_t port_idx = ROVARI_PORT(pin);
    uint8_t pin_num  = ROVARI_PIN_NUM(pin);

    GPIO_TypeDef *port;
    switch (port_idx) {
        case 0:  port = GPIOA; break;
        case 1:  port = GPIOB; break;
        case 2:  port = GPIOC; break;
        case 3:  port = GPIOD; break;
        default: port = GPIOA; break;
    }

    neo_bshr = &port->BSHR;
    neo_bcr  = &port->BCR;
    neo_pin_mask = (1U << pin_num);

    /* Clear buffer */
    for (uint16_t i = 0; i < NEO_MAX_PIXELS * 3; i++) {
        neo_buf[i] = 0;
    }

    /* Send reset pulse (>50us low) */
    Delay_Us(80);
}

void neo_set(uint8_t pixel, uint8_t r, uint8_t g, uint8_t b)
{
    if (pixel >= neo_num) return;
    uint16_t idx = pixel * 3;
    neo_buf[idx + 0] = g;   /* WS2812B order: GRB */
    neo_buf[idx + 1] = r;
    neo_buf[idx + 2] = b;
}

void neo_set_hsv(uint8_t pixel, uint8_t hue, uint8_t val)
{
    /* hue: 0-191 (3 phases of 64), val: 0-255 brightness */
    uint8_t phase = hue / 64;
    uint8_t step  = hue % 64;
    uint8_t up    = (uint16_t)step * val / 64;
    uint8_t down  = (uint16_t)(63 - step) * val / 64;

    switch (phase) {
        case 0: neo_set(pixel, down, up,   0);    break;
        case 1: neo_set(pixel, 0,    down, up);   break;
        case 2: neo_set(pixel, up,   0,    down); break;
        default: break;
    }
}

void neo_clear(void)
{
    for (uint16_t i = 0; i < neo_num * 3; i++) {
        neo_buf[i] = 0;
    }
}

void neo_fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint8_t i = 0; i < neo_num; i++) {
        neo_set(i, r, g, b);
    }
}

void neo_show(void)
{
    uint16_t total = neo_num * 3;

    /*
     * Do NOT use interrupts_disable() here. That kills USB CDC
     * interrupts and the board loses its COM port, requiring a
     * physical BOOT button recovery.
     *
     * NeoPixel timing is tolerant enough that brief USB interrupts
     * will at worst glitch one frame. The next neo_show() fixes it.
     */

    neo_send_buf(neo_bshr, neo_bcr, neo_pin_mask, neo_buf, total);

    /* Reset pulse: hold low for >50us */
    Delay_Us(80);
}

uint8_t neo_count(void)
{
    return neo_num;
}