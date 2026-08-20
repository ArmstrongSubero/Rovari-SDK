/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_opa.h - Op-Amp and Comparator (CH32H417)
 *
 * Three op-amps (OPA1/2/3) with internal PGA and one comparator (CMP).
 *
 * OPA3 recommended for general use (PC2 positive, PA4 output, free pins).
 * OPA1 available on PB0 positive, PA5 output.
 *
 * PGA gains: 1x (follower), 8x, 16x, 32x, 64x.
 *
 * Usage:
 *   opa_init(3, OPA_GAIN_8X);   // OPA3 as 8x PGA
 *   // Apply signal to PC2, read amplified output on PA4
 *
 *   cmp_init(CMP_POS_PB0, CMP_NEG_PB1);
 *   uint8_t result = cmp_read();  // 1 if PB0 > PB1
 */

#ifndef ROVARI_OPA_H
#define ROVARI_OPA_H

#include "rovari_defs.h"

/* -- OPA gain settings ---------------------------------------------------- */
typedef enum {
    OPA_GAIN_1X  = 0,   /* Unity gain (voltage follower) */
    OPA_GAIN_8X  = 1,
    OPA_GAIN_16X = 2,
    OPA_GAIN_32X = 3,
    OPA_GAIN_64X = 4,
} OpaGain;

/* -- CMP input selection -------------------------------------------------- */
typedef enum {
    CMP_POS_PB0 = 0,    /* Positive input on PB0 */
    CMP_POS_PB2 = 1,    /* Positive input on PB2 */
} CmpPositive;

typedef enum {
    CMP_NEG_PB1 = 0,    /* Negative input on PB1 */
} CmpNegative;

/* =========================================================================
 *  C API
 * ========================================================================= */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize an op-amp in PGA mode.
 *
 * @param opa_num  1 (OPA1) or 3 (OPA3). OPA2 has no external pins.
 * @param gain     PGA gain: OPA_GAIN_1X through OPA_GAIN_64X
 *
 * OPA1: input on PB0, output on PA5
 * OPA3: input on PC2, output on PA4
 */
void opa_init(uint8_t opa_num, OpaGain gain);

/**
 * Disable an op-amp.
 */
void opa_stop(uint8_t opa_num);

/**
 * Initialize the comparator.
 * Output available on PB12 and readable via cmp_read().
 *
 * @param pos  Positive input pin selection
 * @param neg  Negative input pin selection
 */
void cmp_init(CmpPositive pos, CmpNegative neg);

/**
 * Read the comparator output.
 * Returns 1 if positive input > negative input, 0 otherwise.
 */
uint8_t cmp_read(void);

/**
 * Disable the comparator.
 */
void cmp_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_OPA_H */
