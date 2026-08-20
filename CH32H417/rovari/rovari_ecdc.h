/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_ecdc.h - Hardware Encryption/Decryption (CH32H417)
 *
 * AES-128/192/256 and SM4 hardware accelerator.
 * No external pins. Operates on 128-bit (4-word) data blocks.
 *
 * Usage:
 *   ecdc_init();
 *   uint32_t key[8] = { ... };
 *   uint32_t plain[4] = { ... };
 *   uint32_t cipher[4];
 *   ecdc_encrypt_block(ECDC_AES, ECDC_KEY_128, key, plain, cipher);
 *   ecdc_decrypt_block(ECDC_AES, ECDC_KEY_128, key, cipher, result);
 */

#ifndef ROVARI_ECDC_H
#define ROVARI_ECDC_H

#include "rovari_defs.h"

/* -- Algorithm selection -------------------------------------------------- */
typedef enum {
    ECDC_AES = 0,
    ECDC_SM4 = 1,
} EcdcAlgo;

/* -- Key length ----------------------------------------------------------- */
typedef enum {
    ECDC_KEY_128 = 0,
    ECDC_KEY_192 = 1,
    ECDC_KEY_256 = 2,
} EcdcKeyLen;

/* =========================================================================
 *  C API
 * ========================================================================= */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the ECDC hardware clock.
 * Call once before any encrypt/decrypt operations.
 */
void ecdc_init(void);

/**
 * Encrypt a single 128-bit block using ECB mode.
 *
 * @param algo    ECDC_AES or ECDC_SM4
 * @param keylen  ECDC_KEY_128, ECDC_KEY_192, or ECDC_KEY_256
 * @param key     Encryption key (8 x uint32_t, unused words zero for shorter keys)
 * @param data    Input plaintext (4 x uint32_t = 128 bits)
 * @param out     Output ciphertext (4 x uint32_t)
 */
void ecdc_encrypt_block(EcdcAlgo algo, EcdcKeyLen keylen,
                        const uint32_t key[8], const uint32_t data[4],
                        uint32_t out[4]);

/**
 * Decrypt a single 128-bit block using ECB mode.
 *
 * @param algo    ECDC_AES or ECDC_SM4
 * @param keylen  ECDC_KEY_128, ECDC_KEY_192, or ECDC_KEY_256
 * @param key     Decryption key (same key used for encryption)
 * @param data    Input ciphertext (4 x uint32_t)
 * @param out     Output plaintext (4 x uint32_t)
 */
void ecdc_decrypt_block(EcdcAlgo algo, EcdcKeyLen keylen,
                        const uint32_t key[8], const uint32_t data[4],
                        uint32_t out[4]);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_ECDC_H */
