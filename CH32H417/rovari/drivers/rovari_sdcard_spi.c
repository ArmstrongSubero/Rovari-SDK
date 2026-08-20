/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_sdcard_spi.c - SD Card over SPI2 with FatFS (CH32H417)
 *
 * Ported from CH32V307 SPI1 SD driver. Uses SPI2 on VDDIO 3.3V domain
 * pins so PSRAM can keep VIO18 at 1.8V permanently.
 *
 * Pins:
 *   PA12 = SPI2_SCK  (AF5) - VDDIO 3.3V
 *   PC1  = SPI2_MOSI (AF5) - VDDIO 3.3V
 *   PC2  = SPI2_MISO (AF5) - VDDIO 3.3V
 *   PC3  = SD_CS     (GPIO)- VDDIO 3.3V
 */

#include "rovari_sdcard_spi.h"
#include "ch32h417.h"
#include "ch32h417_spi.h"
#include "ch32h417_gpio.h"
#include "ch32h417_rcc.h"
#include "diskio.h"
#include "debug.h"
#include <string.h>

/* -- SD commands ---------------------------------------------------- */

#define CMD0    (0x40+0)       /* GO_IDLE_STATE */
#define CMD1    (0x40+1)       /* SEND_OP_COND (MMC) */
#define ACMD41  (0xC0+41)      /* SEND_OP_COND (SDC) */
#define CMD8    (0x40+8)       /* SEND_IF_COND */
#define CMD9    (0x40+9)       /* SEND_CSD */
#define CMD16   (0x40+16)      /* SET_BLOCKLEN */
#define CMD17   (0x40+17)      /* READ_SINGLE_BLOCK */
#define CMD24   (0x40+24)      /* WRITE_BLOCK */
#define CMD55   (0x40+55)      /* APP_CMD */
#define CMD58   (0x40+58)      /* READ_OCR */

/* -- State ---------------------------------------------------------- */

static uint8_t  card_type  = 0;
static uint8_t  mounted    = 0;
static uint8_t  slow_mode  = 1;
static DSTATUS  disk_stat  = STA_NOINIT;
static FATFS    fs_obj;

/* -- Low-level SPI -------------------------------------------------- */

static uint8_t xfer_spi(uint8_t d)
{
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SPI2, d);
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_RXNE) == RESET);
    return (uint8_t)SPI_I2S_ReceiveData(SPI2);
}

static uint8_t xfer_spi_slow(uint8_t d)
{
    uint8_t r = xfer_spi(d);
    Delay_Us(10);
    return r;
}

static void xmit_spi(uint8_t d)
{
    if (slow_mode) xfer_spi_slow(d);
    else xfer_spi(d);
}

static uint8_t rcv_spi(void)
{
    if (slow_mode) return xfer_spi_slow(0xFF);
    else return xfer_spi(0xFF);
}

#define CS_LOW()   (SD_CS_PORT->BCR  = SD_CS_PIN)
#define CS_HIGH()  (SD_CS_PORT->BSHR = SD_CS_PIN)

/* -- SPI2 hardware init --------------------------------------------- */

static void sd_spi_hw_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    SPI_InitTypeDef  spi  = {0};

    /* Enable clocks */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOA |
                          RCC_HB2Periph_GPIOC, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_SPI2 | RCC_HB1Periph_PWR, ENABLE);

    /* PC1/PC2/PC3 are on VIO18 domain. Set to 3.3V for SD card. */
    PWR_VIO18ModeCfg(PWR_VIO18CFGMODE_SW);
    PWR_VIO18LevelCfg(PWR_VIO18Level_MODE3);

    /* CS pin (PC3) - output, high */
    gpio.GPIO_Pin   = SD_CS_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(SD_CS_PORT, &gpio);
    CS_HIGH();

    /* PA12 = SPI2_SCK (AF5) */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource12, GPIO_AF5);
    gpio.GPIO_Pin   = GPIO_Pin_12;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOA, &gpio);

    /* PC1 = SPI2_MOSI (AF5) */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource1, GPIO_AF5);
    gpio.GPIO_Pin   = GPIO_Pin_1;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOC, &gpio);

    /* PC2 = SPI2_MISO (AF5) */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource2, GPIO_AF5);
    gpio.GPIO_Pin   = GPIO_Pin_2;
    gpio.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &gpio);

    /* SPI2: Mode 0, slow clock (prescaler 256) for card init */
    spi.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode              = SPI_Mode_Master;
    spi.SPI_DataSize          = SPI_DataSize_8b;
    spi.SPI_CPOL              = SPI_CPOL_Low;
    spi.SPI_CPHA              = SPI_CPHA_1Edge;
    spi.SPI_NSS               = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_Mode7;  /* /256, slow init */
    spi.SPI_FirstBit          = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial     = 7;
    SPI_Init(SPI2, &spi);
    SPI_Cmd(SPI2, ENABLE);
}

static void sd_spi_set_fast(void)
{
    SPI_Cmd(SPI2, DISABLE);
    SPI2->CTLR1 = (SPI2->CTLR1 & ~SPI_BaudRatePrescaler_Mode7) |
                   SPI_BaudRatePrescaler_Mode2;  /* /8, fast mode */
    SPI_Cmd(SPI2, ENABLE);
    slow_mode = 0;
}

/* -- Card select/deselect ------------------------------------------- */

static void deselect(void)
{
    CS_HIGH();
    rcv_spi();
}

static uint8_t select_card(void)
{
    CS_LOW();
    rcv_spi();

    if (slow_mode) return 1;

    for (uint16_t i = 0; i < 50000; i++) {
        if (rcv_spi() == 0xFF) return 1;
    }
    CS_HIGH();
    return 0;
}

/* -- Send command --------------------------------------------------- */

static uint8_t send_cmd_raw(uint8_t cmd, uint32_t arg)
{
    uint8_t n, res;

    deselect();
    if (!select_card()) return 0xFF;

    xmit_spi(cmd);
    xmit_spi((uint8_t)(arg >> 24));
    xmit_spi((uint8_t)(arg >> 16));
    xmit_spi((uint8_t)(arg >> 8));
    xmit_spi((uint8_t)arg);

    n = 0x01;
    if (cmd == CMD0) n = 0x95;
    if (cmd == CMD8) n = 0x87;
    xmit_spi(n);

    n = 10;
    do { res = rcv_spi(); } while ((res & 0x80) && --n);
    return res;
}

static uint8_t send_cmd(uint8_t cmd, uint32_t arg)
{
    uint8_t res;
    if (cmd & 0x80) {
        cmd &= 0x7F;
        res = send_cmd_raw(CMD55, 0);
        if (res > 1) return res;
    }
    return send_cmd_raw(cmd, arg);
}

/* -- Data blocks ---------------------------------------------------- */

static uint8_t rcvr_datablock(uint8_t *buff, uint16_t btr)
{
    uint8_t token;
    uint16_t tmr = 40000;

    do { token = rcv_spi(); } while (token == 0xFF && --tmr);
    if (token != 0xFE) return 0;

    do { *buff++ = rcv_spi(); } while (--btr);

    rcv_spi();  /* CRC */
    rcv_spi();
    return 1;
}

static uint8_t xmit_datablock(const uint8_t *buff)
{
    uint8_t resp;

    xmit_spi(0xFE);
    for (uint16_t i = 0; i < 512; i++) xmit_spi(buff[i]);

    xmit_spi(0xFF);  /* CRC dummy */
    xmit_spi(0xFF);

    resp = rcv_spi();
    if ((resp & 0x1F) != 0x05) return 0;

    while (rcv_spi() == 0x00) { ; }
    return 1;
}

/* -- Card initialization -------------------------------------------- */

uint8_t sd_disk_initialize(void)
{
    uint8_t  n, ty, ocr[4];
    uint16_t tmr;

    sd_spi_hw_init();
    slow_mode = 1;
    CS_HIGH();

    /* 80 clocks with CS high */
    for (n = 10; n; n--) rcv_spi();

    ty = 0;

    if (send_cmd(CMD0, 0) == 1) {
        if (send_cmd(CMD8, 0x1AA) == 1) {
            for (n = 0; n < 4; n++) ocr[n] = rcv_spi();

            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
                for (tmr = 10000; tmr; tmr--) {
                    if (send_cmd(ACMD41, 1UL << 30) == 0) break;
                    Delay_Us(100);
                }
                if (tmr && send_cmd(CMD58, 0) == 0) {
                    for (n = 0; n < 4; n++) ocr[n] = rcv_spi();
                    ty = (ocr[0] & 0x40) ? (SD_TYPE_SD2 | SD_TYPE_BLOCK) : SD_TYPE_SD2;
                }
            }
        } else {
            uint8_t cmd;
            if (send_cmd(ACMD41, 0) <= 1) { ty = SD_TYPE_SD1; cmd = ACMD41; }
            else { ty = SD_TYPE_MMC; cmd = CMD1; }

            for (tmr = 10000; tmr && send_cmd(cmd, 0); tmr--) Delay_Us(100);
            if (!tmr || send_cmd(CMD16, 512) != 0) ty = 0;
        }
    }

    card_type = ty;
    deselect();

    if (ty) {
        sd_spi_set_fast();
        disk_stat &= (DSTATUS)~STA_NOINIT;
    } else {
        disk_stat = STA_NOINIT;
    }

    return ty ? 0 : 1;
}

/* -- Read / Write --------------------------------------------------- */

uint8_t sd_disk_read(uint8_t *buff, uint32_t sector, uint32_t count)
{
    if (disk_stat & STA_NOINIT) return 1;
    if (!(card_type & SD_TYPE_BLOCK)) sector *= 512;

    while (count--) {
        if (send_cmd(CMD17, sector) != 0) { deselect(); return 1; }
        if (!rcvr_datablock(buff, 512))   { deselect(); return 1; }
        deselect();
        sector++;
        buff += 512;
    }
    return 0;
}

uint8_t sd_disk_write(const uint8_t *buff, uint32_t sector, uint32_t count)
{
    if (disk_stat & STA_NOINIT) return 1;
    if (!(card_type & SD_TYPE_BLOCK)) sector *= 512;

    while (count--) {
        if (send_cmd(CMD24, sector) != 0) { deselect(); return 1; }
        if (!xmit_datablock(buff))        { deselect(); return 1; }
        deselect();
        sector++;
        buff += 512;
    }
    return 0;
}

/* -- Public API ----------------------------------------------------- */

uint8_t sd_init(void)
{
    if (sd_disk_initialize() != 0) return 1;

    FRESULT res = f_mount(&fs_obj, "0:", 1);
    if (res != FR_OK) {
        mounted = 0;
        return 2;
    }

    mounted = 1;
    return 0;
}

void sd_deinit(void)
{
    f_mount(NULL, "0:", 0);
    mounted = 0;
    disk_stat = STA_NOINIT;
}

uint8_t sd_get_card_type(void)
{
    return card_type;
}

uint8_t sd_is_mounted(void)
{
    return mounted;
}

uint32_t sd_get_sector_count(void)
{
    if (!(disk_stat & ~STA_PROTECT)) {
        uint8_t csd[16];
        if (send_cmd(CMD9, 0) == 0 && rcvr_datablock(csd, 16)) {
            deselect();
            if ((csd[0] >> 6) == 1) {
                /* SDv2 (SDHC/SDXC) */
                uint32_t c_size = ((uint32_t)(csd[7] & 0x3F) << 16) |
                                  ((uint32_t)csd[8] << 8) | csd[9];
                return (c_size + 1) * 1024;
            } else {
                /* SDv1 */
                uint8_t n = (csd[5] & 0x0F) + ((csd[10] & 0x80) >> 7) +
                            ((csd[9] & 0x03) << 1) + 2;
                uint32_t c_size = ((uint32_t)(csd[6] & 0x03) << 10) |
                                  ((uint32_t)csd[7] << 2) |
                                  ((csd[8] & 0xC0) >> 6);
                return (c_size + 1) << (n - 9);
            }
        }
        deselect();
    }
    return 0;
}
