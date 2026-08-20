/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 */

/**
 * @file rovari_sdcard.c
 * @brief SD card over SPI with FatFs for CH32V307 (SEVS-Core).
 *
 * Direct WCH SPI HAL for performance. Pins SPI1 PA5/PA6/PA7, CS PB8.
 * Every SPI flag poll and card-busy wait is bounded so a missing or
 * faulty card cannot hang the CPU.
 *
 * @copyright (c) 2025 Armstrong Subero
 */

/* System includes */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* SEVS runtime */
#include "sevs_runtime.h"

/* Project includes (WCH HAL) */
#include "ch32v30x.h"
#include "ch32v30x_spi.h"
#include "ch32v30x_gpio.h"
#include "ch32v30x_rcc.h"
#include "debug.h"

/* Vendor includes (FatFs) */
#include "diskio.h"

/* Local includes */
#include "rovari_sdcard.h"

/* ── SD commands ─────────────────────────────────────────────────────── */
#define CMD0    (0x40+0)
#define CMD1    (0x40+1)
#define ACMD41  (0xC0+41)
#define CMD8    (0x40+8)
#define CMD9    (0x40+9)
#define CMD16   (0x40+16)
#define CMD17   (0x40+17)
#define CMD24   (0x40+24)
#define CMD55   (0x40+55)
#define CMD58   (0x40+58)

/* Bounded poll caps. */
#define SD_SPI_TIMEOUT   100000U
#define SD_READY_TIMEOUT 50000U
#define SD_TOKEN_TIMEOUT 40000U
#define SD_BUSY_TIMEOUT  500000U
#define SD_BLOCK_SIZE    512U

/* ── State ───────────────────────────────────────────────────────────── */
static uint8_t  card_type  = 0;
static uint8_t  mounted    = 0;
static uint8_t  slow_mode  = 1;
static DSTATUS  disk_stat  = STA_NOINIT;
static FATFS    fs_obj;

/* ── Low-level SPI ───────────────────────────────────────────────────── */

/**
 * @brief Exchange one SPI byte (bounded waits).
 * @return Byte received, or 0xFF on timeout.
 * @req REQ-ROVARI-SDCARD-0020
 */
static uint8_t xfer_spi(uint8_t d)
{
    uint8_t txe_ok = 0;
    for (uint32_t i = 0U; i < SD_SPI_TIMEOUT; i++) {
        if (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) != RESET) {
            txe_ok = 1;
            break;
        }
    }
    if (!txe_ok) {
        return 0xFF;
    }
    SPI_I2S_SendData(SPI1, d);

    for (uint32_t i = 0U; i < SD_SPI_TIMEOUT; i++) {
        if (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) != RESET) {
            return (uint8_t)SPI_I2S_ReceiveData(SPI1);
        }
    }
    return 0xFF;
}

/**
 * @brief Exchange one SPI byte with an inter-byte delay (slow init mode).
 */
static uint8_t xfer_spi_slow(uint8_t d)
{
    uint8_t r = xfer_spi(d);
    Delay_Us(10);
    return r;
}

/**
 * @brief Transmit one byte using the current speed mode.
 */
static void xmit_spi(uint8_t d)
{
    if (slow_mode) {
        (void)xfer_spi_slow(d);
    } else {
        (void)xfer_spi(d);
    }
}

/**
 * @brief Receive one byte (clock out 0xFF) using the current speed mode.
 */
static uint8_t rcv_spi(void)
{
    return slow_mode ? xfer_spi_slow(0xFF) : xfer_spi(0xFF);
}

#define CS_LOW()   GPIO_WriteBit(SD_CS_PORT, SD_CS_PIN, Bit_RESET)
#define CS_HIGH()  GPIO_WriteBit(SD_CS_PORT, SD_CS_PIN, Bit_SET)

/* ── SPI hardware init ───────────────────────────────────────────────── */

/**
 * @brief Configure SPI1 and the SD card GPIO lines for slow-clock init.
 */
static void sd_spi_hw_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    SPI_InitTypeDef  SPI_InitStructure  = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_SPI1, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = SD_CS_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SD_CS_PORT, &GPIO_InitStructure);
    CS_HIGH();

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_256;
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial     = 7;
    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_Cmd(SPI1, ENABLE);
}

/**
 * @brief Switch SPI1 to fast clock after successful card init.
 */
static void sd_spi_set_fast(void)
{
    SPI_Cmd(SPI1, DISABLE);
    SPI1->CTLR1 = (SPI1->CTLR1 & ~SPI_BaudRatePrescaler_256) |
                   SPI_BaudRatePrescaler_8;
    SPI_Cmd(SPI1, ENABLE);
    slow_mode = 0;
}

/* ── Card select/deselect ────────────────────────────────────────────── */

/**
 * @brief Deselect the card and clock one trailing byte.
 */
static void deselect(void)
{
    CS_HIGH();
    (void)rcv_spi();
}

/**
 * @brief Select the card and wait (bounded) for it to be ready.
 * @return 1 if selected/ready, 0 on timeout.
 * @req REQ-ROVARI-SDCARD-0020
 */
static uint8_t select_card(void)
{
    CS_LOW();
    (void)rcv_spi();

    if (slow_mode) {
        return 1;
    }

    for (uint32_t i = 0U; i < SD_READY_TIMEOUT; i++) {
        if (rcv_spi() == 0xFF) {
            return 1;
        }
    }
    CS_HIGH();
    return 0;
}

/* ── Send command ────────────────────────────────────────────────────── */

/**
 * @brief Send a raw SD command and return the R1 response.
 */
static uint8_t send_cmd_raw(uint8_t cmd, uint32_t arg)
{
    uint8_t n;
    uint8_t res;

    deselect();
    if (!select_card()) {
        return 0xFF;
    }
    SEVS_INVARIANT((cmd & 0x40U) != 0U);  /* SD command framing bit set */

    xmit_spi(cmd);
    xmit_spi((uint8_t)(arg >> 24));
    xmit_spi((uint8_t)(arg >> 16));
    xmit_spi((uint8_t)(arg >> 8));
    xmit_spi((uint8_t)arg);

    n = 0x01;
    if (cmd == CMD0) { n = 0x95; }
    if (cmd == CMD8) { n = 0x87; }
    xmit_spi(n);

    n = 10;
    /* @sevs-bound: at most 10 iterations (n counts down from 10). */
    do {
        res = rcv_spi();
    } while ((res & 0x80) && --n);
    return res;
}

/**
 * @brief Send an SD command, handling the ACMD prefix.
 */
static uint8_t send_cmd(uint8_t cmd, uint32_t arg)
{
    uint8_t res;
    if (cmd & 0x80) {
        cmd &= 0x7F;
        res = send_cmd_raw(CMD55, 0);
        if (res > 1) {
            return res;
        }
    }
    return send_cmd_raw(cmd, arg);
}

/* ── Data blocks ─────────────────────────────────────────────────────── */

/**
 * @brief Receive a data block after its start token (bounded token wait).
 * @param[out] buff Destination buffer.
 * @param[in]  btr  Bytes to read.
 * @return 1 on success, 0 on token timeout.
 * @req REQ-ROVARI-SDCARD-0020
 * @req REQ-ROVARI-SDCARD-0021
 */
static uint8_t rcvr_datablock(uint8_t *buff, uint16_t btr)
{
    SEVS_REQUIRE_NOT_NULL(buff);
    SEVS_INVARIANT(btr > 0U);
    uint8_t token = 0xFF;

    for (uint32_t i = 0U; i < SD_TOKEN_TIMEOUT; i++) {
        token = rcv_spi();
        if (token != 0xFF) {
            break;
        }
    }
    if (token != 0xFE) {
        return 0;
    }

    /* @sevs-bound: exactly btr iterations (btr is the caller's byte count). */
    do {
        *buff++ = rcv_spi();
    } while (--btr);

    (void)rcv_spi();  /* CRC */
    (void)rcv_spi();
    return 1;
}

/**
 * @brief Transmit a 512-byte data block and wait (bounded) for completion.
 * @param[in] buff Source buffer (512 bytes).
 * @return 1 on success, 0 on rejected/timed-out write.
 * @req REQ-ROVARI-SDCARD-0020
 * @req REQ-ROVARI-SDCARD-0021
 */
static uint8_t xmit_datablock(const uint8_t *buff)
{
    SEVS_REQUIRE_NOT_NULL(buff);
    uint8_t resp;

    xmit_spi(0xFE);
    for (uint16_t i = 0; i < SD_BLOCK_SIZE; i++) {
        xmit_spi(buff[i]);
    }

    xmit_spi(0xFF);  /* CRC dummy */
    xmit_spi(0xFF);

    resp = rcv_spi();
    if ((resp & 0x1F) != 0x05) {
        return 0;
    }

    /* Wait (bounded) for the card to finish programming. */
    for (uint32_t i = 0U; i < SD_BUSY_TIMEOUT; i++) {
        if (rcv_spi() != 0x00) {
            return 1;
        }
    }
    return 0;  /* Card stayed busy: treat as failure rather than hang. */
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Card initialization (called by diskio)
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * @brief Initialize the SD card over SPI and detect its type.
 * @return 0 on success, 1 on failure.
 * @req REQ-ROVARI-SDCARD-0010
 * @req REQ-ROVARI-SDCARD-0020
 */
uint8_t sd_disk_initialize(void)
{
    uint8_t  n;
    uint8_t  ty;
    uint8_t  ocr[4];
    uint16_t tmr;

    sd_spi_hw_init();
    slow_mode = 1;
    CS_HIGH();

    /* 80 clocks with CS high */
    for (n = 10; n; n--) {
        (void)rcv_spi();
    }

    ty = 0;

    if (send_cmd(CMD0, 0) == 1) {
        if (send_cmd(CMD8, 0x1AA) == 1) {
            for (n = 0; n < 4; n++) {
                ocr[n] = rcv_spi();
            }

            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
                for (tmr = 10000; tmr; tmr--) {
                    if (send_cmd(ACMD41, 1UL << 30) == 0) {
                        break;
                    }
                    Delay_Us(100);
                }
                if (tmr && send_cmd(CMD58, 0) == 0) {
                    for (n = 0; n < 4; n++) {
                        ocr[n] = rcv_spi();
                    }
                    ty = (ocr[0] & 0x40) ? (SD_TYPE_SD2 | SD_TYPE_BLOCK) : SD_TYPE_SD2;
                }
            }
        } else {
            uint8_t cmd;
            if (send_cmd(ACMD41, 0) <= 1) {
                ty = SD_TYPE_SD1;
                cmd = ACMD41;
            } else {
                ty = SD_TYPE_MMC;
                cmd = CMD1;
            }

            for (tmr = 10000; tmr && send_cmd(cmd, 0); tmr--) {
                Delay_Us(100);
            }
            if (!tmr || send_cmd(CMD16, 512) != 0) {
                ty = 0;
            }
        }
    }

    card_type = ty;
    SEVS_INVARIANT((ty == 0U) || ((ty & (SD_TYPE_SD1 | SD_TYPE_SD2 | SD_TYPE_MMC)) != 0U));
    deselect();

    if (ty) {
        sd_spi_set_fast();
        disk_stat &= (DSTATUS)~STA_NOINIT;
    } else {
        disk_stat = STA_NOINIT;
    }

    return ty ? 0 : 1;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Read / Write (called by diskio)
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * @brief Read count 512-byte sectors from the card.
 * @param[out] buff   Destination buffer (count*512 bytes).
 * @param[in]  sector Start sector (LBA).
 * @param[in]  count  Sector count.
 * @return 0 on success, 1 on error.
 * @req REQ-ROVARI-SDCARD-0011
 * @req REQ-ROVARI-SDCARD-0021
 */
uint8_t sd_disk_read(uint8_t *buff, uint32_t sector, uint32_t count)
{
    SEVS_REQUIRE_NOT_NULL(buff);
    if (disk_stat & STA_NOINIT) {
        return 1;
    }
    if (!(card_type & SD_TYPE_BLOCK)) {
        sector *= 512;
    }

    /* @sevs-bound: exactly the caller-supplied sector count iterations. */
    while (count--) {
        if (send_cmd(CMD17, sector) != 0) { deselect(); return 1; }
        if (!rcvr_datablock(buff, 512))   { deselect(); return 1; }
        deselect();
        sector++;
        buff += 512;
    }
    return 0;
}

/**
 * @brief Write count 512-byte sectors to the card.
 * @param[in] buff   Source buffer (count*512 bytes).
 * @param[in] sector Start sector (LBA).
 * @param[in] count  Sector count.
 * @return 0 on success, 1 on error.
 * @req REQ-ROVARI-SDCARD-0011
 * @req REQ-ROVARI-SDCARD-0021
 */
uint8_t sd_disk_write(const uint8_t *buff, uint32_t sector, uint32_t count)
{
    SEVS_REQUIRE_NOT_NULL(buff);
    if (disk_stat & STA_NOINIT) {
        return 1;
    }
    if (!(card_type & SD_TYPE_BLOCK)) {
        sector *= 512;
    }

    /* @sevs-bound: exactly the caller-supplied sector count iterations. */
    while (count--) {
        if (send_cmd(CMD24, sector) != 0) { deselect(); return 1; }
        if (!xmit_datablock(buff))        { deselect(); return 1; }
        deselect();
        sector++;
        buff += 512;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * @brief Initialize the card and mount FatFs on drive 0.
 * @return 0 on success, 1 if card init failed, 2 if mount failed.
 * @req REQ-ROVARI-SDCARD-0012
 */
uint8_t sd_init(void)
{
    if (sd_disk_initialize() != 0) {
        return 1;
    }

    FRESULT res = f_mount(&fs_obj, "0:", 1);
    if (res != FR_OK) {
        mounted = 0;
        return 2;
    }

    mounted = 1;
    return 0;
}

/**
 * @brief Unmount FatFs and mark the card uninitialized.
 * @req REQ-ROVARI-SDCARD-0012
 */
void sd_deinit(void)
{
    f_mount(NULL, "0:", 0);
    mounted = 0;
    disk_stat = STA_NOINIT;
}

/**
 * @brief Report the detected card type bitmask.
 * @return Card type flags.
 * @req REQ-ROVARI-SDCARD-0013
 */
uint8_t sd_get_card_type(void)
{
    return card_type;
}

/**
 * @brief Report whether a filesystem is currently mounted.
 * @return 1 if mounted, 0 otherwise.
 * @req REQ-ROVARI-SDCARD-0013
 */
uint8_t sd_is_mounted(void)
{
    return mounted;
}
