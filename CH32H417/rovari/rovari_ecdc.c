/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_ecdc.c - Hardware Encryption/Decryption (CH32H417)
 *
 * The ECDC peripheral provides hardware-accelerated:
 *   - AES-128/192/256 encryption and decryption
 *   - SM4 encryption and decryption (Chinese national standard)
 *   - ECB and CTR block cipher modes
 *
 * No external pins needed. All operations are memory-to-memory.
 * ECDC is on the HB2 bus.
 */

#include "rovari_ecdc.h"
#include "debug.h"

/* =========================================================================
 *  Public API
 * ========================================================================= */

void ecdc_init(void)
{
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_ECDC, ENABLE);
    ECDC_HardwareClockCmd(ENABLE);
    ECDC_ClockConfig(ECDC_ClockSource_PLLCLK_Div6);
}

void ecdc_encrypt_block(EcdcAlgo algo, EcdcKeyLen keylen,
                        const uint32_t key[8], const uint32_t data[4],
                        uint32_t out[4])
{
    ECDC_KEY_TypeDef k;
    k.KEY_31T0   = key[0];
    k.KEY_63T32  = key[1];
    k.KEY_95T64  = key[2];
    k.KEY_127T96 = key[3];
    k.KEY_159T128 = key[4];
    k.KEY_191T160 = key[5];
    k.KEY_223T192 = key[6];
    k.KEY_255T224 = key[7];

    ECDC_IV_TypeDef iv = {0};

    ECDC_InitTypeDef ecdc = {0};
    ecdc.Algorithm = (algo == ECDC_AES) ? ECDCAlgorithm_AES : ECDCAlgorithm_SM4;
    ecdc.BlockCipherMode = ECDCBlockCipherMode_ECB;
    ecdc.ExcuteMode = ECDC_SingleTime_Encrypt;
    ecdc.ExcuteEndian = ECDCExcuteEndian_Big;
    ecdc.Key = &k;
    ecdc.IV = &iv;

    switch (keylen) {
        case ECDC_KEY_128: ecdc.KeyLen = ECDCKeyLen_128b; break;
        case ECDC_KEY_192: ecdc.KeyLen = ECDCKeyLen_192b; break;
        case ECDC_KEY_256: ecdc.KeyLen = ECDCKeyLen_256b; break;
    }

    ECDC_ClearFlag(ECDC_FLAG_Single_END);
    ECDC_Init(&ecdc);
    ECDC_SingleWR_RawData((uint32_t *)data);

    while (!ECDC_GetFlagStatus(ECDC_FLAG_Single_END));
    ECDC_ClearFlag(ECDC_FLAG_Single_END);
    ECDC_SingleRD_EcdcData(out);
}

void ecdc_decrypt_block(EcdcAlgo algo, EcdcKeyLen keylen,
                        const uint32_t key[8], const uint32_t data[4],
                        uint32_t out[4])
{
    ECDC_KEY_TypeDef k;
    k.KEY_31T0   = key[0];
    k.KEY_63T32  = key[1];
    k.KEY_95T64  = key[2];
    k.KEY_127T96 = key[3];
    k.KEY_159T128 = key[4];
    k.KEY_191T160 = key[5];
    k.KEY_223T192 = key[6];
    k.KEY_255T224 = key[7];

    ECDC_IV_TypeDef iv = {0};

    ECDC_InitTypeDef ecdc = {0};
    ecdc.Algorithm = (algo == ECDC_AES) ? ECDCAlgorithm_AES : ECDCAlgorithm_SM4;
    ecdc.BlockCipherMode = ECDCBlockCipherMode_ECB;
    ecdc.ExcuteMode = ECDC_SingleTime_Decrypt;
    ecdc.ExcuteEndian = ECDCExcuteEndian_Big;
    ecdc.Key = &k;
    ecdc.IV = &iv;

    switch (keylen) {
        case ECDC_KEY_128: ecdc.KeyLen = ECDCKeyLen_128b; break;
        case ECDC_KEY_192: ecdc.KeyLen = ECDCKeyLen_192b; break;
        case ECDC_KEY_256: ecdc.KeyLen = ECDCKeyLen_256b; break;
    }

    ECDC_ClearFlag(ECDC_FLAG_Single_END);
    ECDC_Init(&ecdc);
    ECDC_SingleWR_RawData((uint32_t *)data);

    while (!ECDC_GetFlagStatus(ECDC_FLAG_Single_END));
    ECDC_ClearFlag(ECDC_FLAG_Single_END);
    ECDC_SingleRD_EcdcData(out);
}
