/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_sdcard.c - SD Card via SDMMC peripheral (CH32H417)
 *
 * Native SDMMC hardware driver with DMA for 4-bit wide bus SD access.
 * Replaces the SPI-based SD driver used on CH32V307.
 *
 * Default pins (SDMMC, no remap):
 *   PC12 = SDCK   (clock)       - dedicated, no AF
 *   PD2  = SDCMD  (command)     - dedicated, no AF
 *   PC8  = SDD0   (data 0)      - dedicated, no AF
 *   PC9  = SDD1   (data 1)      - AF8
 *   PC10 = SDD2   (data 2)      - AF8
 *   PC11 = SDD3   (data 3)      - AF8
 *
 * Init sequence: CMD0 -> CMD8 -> ACMD41 -> CMD2 -> CMD3 -> CMD9 ->
 *                CMD7 -> ACMD6 (4-bit bus) -> speed up clock
 *
 * Data transfers use the SDMMC built-in DMA engine (DMA_BEG1 register).
 * Block size is always 512 bytes.
 */

#include "rovari_sdcard.h"
#include "ch32h417.h"
#include "ch32h417_sdmmc.h"
#include "ch32h417_gpio.h"
#include "ch32h417_rcc.h"
#include "ch32h417_pwr.h"
#include "diskio.h"
#include "debug.h"
#include <string.h>

/* -- SD command indices --------------------------------------------- */

#define CMD0    0       /* GO_IDLE_STATE */
#define CMD2    2       /* ALL_SEND_CID */
#define CMD3    3       /* SEND_RELATIVE_ADDR */
#define CMD7    7       /* SELECT_CARD */
#define CMD8    8       /* SEND_IF_COND */
#define CMD9    9       /* SEND_CSD */
#define CMD12   12      /* STOP_TRANSMISSION */
#define CMD13   13      /* SEND_STATUS */
#define CMD16   16      /* SET_BLOCKLEN */
#define CMD17   17      /* READ_SINGLE_BLOCK */
#define CMD18   18      /* READ_MULTIPLE_BLOCK */
#define CMD24   24      /* WRITE_BLOCK */
#define CMD25   25      /* WRITE_MULTIPLE_BLOCK */
#define CMD55   55      /* APP_CMD */
#define CMD58   58      /* READ_OCR */
#define ACMD6   6       /* SET_BUS_WIDTH */
#define ACMD41  41      /* SD_SEND_OP_COND */

/* -- Timeouts ------------------------------------------------------- */

#define CMD_TIMEOUT     0xFFFFF    /* EVT: SDMMC_TOUT_TIMES */
#define DATA_TIMEOUT    0xFFFFF

/* -- Internal state ------------------------------------------------- */

static uint8_t   card_type    = 0;
static uint8_t   mounted      = 0;
static uint16_t  card_rca     = 0;
static uint32_t  sector_count = 0;
static DSTATUS   disk_stat    = STA_NOINIT;
static FATFS     fs_obj __attribute__((aligned(16)));

/* Separate DMA buffers for read and write to prevent corruption
 * during FatFS read-modify-write cycles (e.g. updating FAT + directory).
 * Must be 16-byte aligned for SDMMC DMA engine. */
static uint8_t __attribute__((aligned(16))) dma_rd_buf[512];
static uint8_t __attribute__((aligned(16))) dma_wr_buf[512];

/* ===================================================================
 *  Low-level SDMMC helpers
 * =================================================================== */

static void sdmmc_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Enable SDMMC + DMA1 peripheral clocks */
    RCC_HBPeriphClockCmd(RCC_HBPeriph_SDMMC | RCC_HBPeriph_DMA1, ENABLE);

    /*
     * WCH EVT quirk: SWPMI->OR bit 0 must be set to enable the SDMMC
     * pin mux. Without this, SDMMC pins are not connected to the peripheral.
     */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_SWPMI, ENABLE);
    SWPMI->OR |= (1 << 0);

    /*
     * SDMMC default pin mapping (from EVT SDMMC_SD example):
     *   PC8  = SDMMC_D0    (data 0)
     *   PC9  = SDMMC_D1    (data 1)
     *   PC10 = SDMMC_D2    (data 2)
     *   PC11 = SDMMC_D3    (data 3)
     *   PC12 = SDMMC_SDCK  (clock)
     *   PD2  = SDMMC_CMD   (command)
     *
     * WCH EVT does NOT use GPIO_PinAFConfig for any of these pins.
     * All six are set to GPIO_Mode_AF_PP and it just works.
     * SDMMC is the default alternate function for these pins.
     */

    /* PC8-PC12: D0, D1, D2, D3, CLK */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOC | RCC_HB2Periph_AFIO, ENABLE);
    gpio.GPIO_Pin   = GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 |
                      GPIO_Pin_11 | GPIO_Pin_12;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOC, &gpio);

    /* PD2: CMD */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOD, ENABLE);
    gpio.GPIO_Pin   = GPIO_Pin_2;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOD, &gpio);
}

static void sdmmc_hw_init_slow(void)
{
    SDMMC_InitTypeDef init = {0};

    /* Enable SDMMC peripheral clock (already done in gpio_init, but safe) */
    RCC_HBPeriphClockCmd(RCC_HBPeriph_SDMMC, ENABLE);

    /* Reset SDMMC */
    SDMMC_DeInit();

    /* Enable clock output first (EVT does this before Init) */
    SDMMC_ClockCmd(ENABLE);

    /* Configure for identification mode: 1-bit bus, slow clock
     * Matches EVT SDMMC_SD example exactly */
    init.SDMMC_Mode         = SDMMC_Mode_Host;
    init.SDMMC_PhaseInv     = SDMMC_Phase_No_Inverse;
    init.SDMMC_ClockSpeed   = SDMMC_ClockSpeed_Low;
    init.SDMMC_BusWidth     = SDMMC_BusWidth_1;
    init.SDMMC_ClockEdge    = SDMMC_SampleClock_Falling;   /* EVT uses Falling */
    init.SDMMC_ClockDiv     = 0x1F;   /* maximum divider for slow init */
    init.SDMMC_TimeOut      = 0x0C;   /* EVT uses 0x0C */
    init.SDMMC_SlaveForceCrc_ERR = DISABLE;
    init.SDMMC_DMA_EN       = ENABLE;
    init.SDMMC_Clock_OE     = ENABLE;

    SDMMC_Init(&init);
    SDMMC_ClockCmd(ENABLE);
}

static void sdmmc_set_fast_4bit(void)
{
    /* Switch to 4-bit bus, high speed
     * EVT uses SDMMC_SetClockSpeed(SDMMC_ClockSpeed_High, 12) */
    SDMMC_SetBusWidth(SDMMC_BusWidth_4);
    SDMMC_SetClockSpeed(SDMMC_ClockSpeed_High, 12);
}

/* -- Command send/response ------------------------------------------ */

/**
 * Send an SD command and wait for response.
 * Matches EVT CmdRespError() pattern.
 */
static uint8_t sdmmc_send_cmd(uint8_t cmd_idx, uint32_t arg, uint16_t resp)
{
    SDMMC_CMDInitTypeDef cmd = {0};

    cmd.SDMMC_CMDIdx     = cmd_idx;
    cmd.SDMMC_Argument   = arg;
    cmd.SDMMC_RespExpect = resp;

    SDMMC_CommandConfig(&cmd);

    /* Wait for command done - matches EVT CmdRespError() */
    volatile uint32_t timeout = CMD_TIMEOUT;
    while (timeout--) {
        uint16_t flags = SDMMC->INT_FG & (SDMMC_FLAG_CMDDONE | SDMMC_FLAG_REIDX_ER
                                         | SDMMC_FLAG_RECRC_WR | SDMMC_FLAG_RE_TMOUT);
        if (flags) {
            /* Clear ONLY command-related flags + 0x600 (EVT pattern).
             * Do NOT clear data transfer flags (TRANDONE/TRANERR/DATTMO/etc) */
            SDMMC->INT_FG = (SDMMC_FLAG_CMDDONE | SDMMC_FLAG_REIDX_ER
                           | SDMMC_FLAG_RECRC_WR | SDMMC_FLAG_RE_TMOUT) | 0x600;

            if (flags & SDMMC_FLAG_CMDDONE) return 0;
            if (flags & SDMMC_FLAG_RE_TMOUT) return 1;
            if (flags & SDMMC_FLAG_RECRC_WR) return 2;
            if (flags & SDMMC_FLAG_REIDX_ER) return 3;
        }
    }
    return 1;
}

/** Send CMD2 (ALL_SEND_CID) - 136-bit response */
static uint8_t sd_cmd2(void)
{
    return sdmmc_send_cmd(CMD2, 0, SDMMC_Resp_136);
}

/** Send CMD3 (SEND_RELATIVE_ADDR) - gets RCA */
static uint8_t sd_cmd3(void)
{
    uint8_t err = sdmmc_send_cmd(CMD3, 0, SDMMC_Resp_48);
    if (err) return err;

    /* EVT reads RCA from Response3, not Response0 */
    card_rca = (uint16_t)(SDMMC_GetResponse(Response3) >> 16);
    return 0;
}

/** Send CMD9 (SEND_CSD) to read card capacity */
static uint8_t sd_cmd9(void)
{
    uint8_t err = sdmmc_send_cmd(CMD9, (uint32_t)card_rca << 16, SDMMC_Resp_136);
    if (err) return err;

    /* EVT reads CSD_Tab[0]=Response3, [1]=Response2, [2]=Response1, [3]=Response0 */
    uint32_t csd[4];
    csd[0] = SDMMC_GetResponse(Response3);
    csd[1] = SDMMC_GetResponse(Response2);
    csd[2] = SDMMC_GetResponse(Response1);
    csd[3] = SDMMC_GetResponse(Response0);

    /* CSD structure version is in bits [127:126] of the 128-bit CSD.
     * With EVT register ordering: csd[0] has the LSB end, csd[3] has MSB end.
     * CSD_STRUCTURE is in csd[3] bits [7:6] (top byte of Response0). */
    uint8_t csd_ver = (uint8_t)((csd[3] >> 6) & 0x03);

    if (csd_ver == 1) {
        /* CSD v2.0 (SDHC/SDXC) */
        /* C_SIZE is bits [69:48] of 128-bit CSD
         * With this register layout, C_SIZE spans csd[1] and csd[2] */
        uint32_t c_size = ((csd[1] >> 16) & 0xFFFF) | ((csd[2] & 0x3F) << 16);
        sector_count = (c_size + 1) * 1024;
    } else {
        /* CSD v1.0 (SDSC) - compute from C_SIZE, C_SIZE_MULT, READ_BL_LEN */
        uint8_t read_bl_len = (uint8_t)((csd[2] >> 16) & 0x0F);
        uint32_t c_size = ((csd[2] & 0x03FF) << 2) | ((csd[1] >> 30) & 0x03);
        uint8_t c_size_mult = (uint8_t)((csd[1] >> 15) & 0x07);
        uint32_t capacity = (c_size + 1) << (c_size_mult + 2 + read_bl_len);
        sector_count = capacity / 512;
    }

    return 0;
}

/** Send CMD7 (SELECT_CARD) to enter transfer state */
static uint8_t sd_cmd7(void)
{
    return sdmmc_send_cmd(CMD7, (uint32_t)card_rca << 16, SDMMC_Resp_R1b);
}

/** Send ACMD6 to set 4-bit bus width */
static uint8_t sd_acmd6_4bit(void)
{
    uint8_t err;

    err = sdmmc_send_cmd(CMD55, (uint32_t)card_rca << 16, SDMMC_Resp_48);
    if (err) return err;

    /* Argument 0x02 = 4-bit bus */
    err = sdmmc_send_cmd(ACMD6, 0x02, SDMMC_Resp_48);
    return err;
}

/** Set block length to 512 (for SDSC cards) */
static uint8_t sd_cmd16(void)
{
    return sdmmc_send_cmd(CMD16, 512, SDMMC_Resp_48);
}

/* -- Wait for card ready (DAT0 line high) --------------------------- */

static uint8_t sd_wait_ready(void)
{
    volatile uint32_t timeout = DATA_TIMEOUT;
    while (timeout--) {
        if (SDMMC_GetStatus_LineData0() == SET) return 0;
    }
    return 1;
}

/* ===================================================================
 *  Data transfer via SDMMC DMA
 * =================================================================== */

static uint8_t sdmmc_read_block(uint8_t *buf, uint32_t sector)
{
    SDMMC_TranModeTypeDef tran = {0};
    uint8_t err;

    /* Wait for card to be ready (EVT: SDMMC_WaitData0) */
    if (sd_wait_ready()) return 1;

    /* MUST Config to 0 first (from EVT comment) */
    SDMMC_BlockConfig(0, 0);

    /* Configure transfer mode: receive direction */
    tran.TranMode_Direction     = SDMMC_TranDir_Receive;
    tran.TranMode_DualDMA       = DISABLE;
    tran.TranMode_AutoGapStop   = DISABLE;
    tran.TranMode_GapStop       = DISABLE;
    tran.TranMode_Boot          = DISABLE;
    tran.TranMode_DDR_EN        = DISABLE;
    tran.TranMode_DDR_ClockFall_Check = DISABLE;
    SDMMC_TranMode_Init(&tran);

    /* Set block config and DMA address */
    SDMMC_BlockConfig(512, 1);
    SDMMC_SetDMAAddr1((uint32_t)buf);

    /* Send CMD17 (READ_SINGLE_BLOCK) */
    err = sdmmc_send_cmd(CMD17, sector, SDMMC_Resp_48);
    if (err) return err;

    /* Wait for data transfer complete - matches EVT DataTransErr() */
    volatile uint32_t timeout = DATA_TIMEOUT;
    while (timeout--) {
        uint16_t flags = SDMMC->INT_FG & (SDMMC_FLAG_TRANDONE | SDMMC_FLAG_BKGAP
                                         | SDMMC_FLAG_TRANERR | SDMMC_FLAG_FIFO_OV
                                         | SDMMC_FLAG_DATTMO);
        if (flags) {
            /* Clear data flags (EVT pattern) */
            SDMMC->INT_FG = (SDMMC_FLAG_TRANDONE | SDMMC_FLAG_BKGAP
                           | SDMMC_FLAG_TRANERR | SDMMC_FLAG_FIFO_OV
                           | SDMMC_FLAG_DATTMO) | 0x600;

            if (flags & SDMMC_FLAG_TRANDONE) return 0;
            if (flags & SDMMC_FLAG_BKGAP) return 0;  /* EVT treats as OK */
            if (flags & SDMMC_FLAG_TRANERR) return 2;
            if (flags & SDMMC_FLAG_FIFO_OV) return 3;
            if (flags & SDMMC_FLAG_DATTMO) return 4;
        }
    }
    return 1;  /* timeout */
}

static uint8_t sdmmc_write_block(const uint8_t *buf, uint32_t sector)
{
    SDMMC_TranModeTypeDef tran = {0};
    uint8_t err;

    /* Wait for card ready */
    if (sd_wait_ready()) return 1;

    /* EVT sends CMD24 BEFORE configuring the transfer mode for writes */
    err = sdmmc_send_cmd(CMD24, sector, SDMMC_Resp_48);
    if (err) return err;

    /* MUST Config to 0 first */
    SDMMC_BlockConfig(0, 0);

    /* Configure transfer mode: send direction */
    tran.TranMode_Direction     = SDMMC_TranDir_Send;
    tran.TranMode_DualDMA       = DISABLE;
    tran.TranMode_AutoGapStop   = DISABLE;
    tran.TranMode_GapStop       = DISABLE;
    tran.TranMode_Boot          = DISABLE;
    tran.TranMode_DDR_EN        = DISABLE;
    tran.TranMode_DDR_ClockFall_Check = DISABLE;
    SDMMC_TranMode_Init(&tran);

    /* Set block config and DMA address */
    SDMMC_BlockConfig(512, 1);
    SDMMC_SetDMAAddr1((uint32_t)buf);

    /* Wait for transfer complete - matches EVT DataTransErr() */
    volatile uint32_t timeout = DATA_TIMEOUT;
    while (timeout--) {
        uint16_t flags = SDMMC->INT_FG & (SDMMC_FLAG_TRANDONE | SDMMC_FLAG_BKGAP
                                         | SDMMC_FLAG_TRANERR | SDMMC_FLAG_FIFO_OV
                                         | SDMMC_FLAG_DATTMO);
        if (flags) {
            SDMMC->INT_FG = (SDMMC_FLAG_TRANDONE | SDMMC_FLAG_BKGAP
                           | SDMMC_FLAG_TRANERR | SDMMC_FLAG_FIFO_OV
                           | SDMMC_FLAG_DATTMO) | 0x600;

            if (flags & SDMMC_FLAG_TRANDONE) {
                sd_wait_ready();
                return 0;
            }
            if (flags & SDMMC_FLAG_BKGAP) {
                sd_wait_ready();
                return 0;
            }
            if (flags & SDMMC_FLAG_TRANERR) return 2;
            if (flags & SDMMC_FLAG_FIFO_OV) return 3;
            if (flags & SDMMC_FLAG_DATTMO) return 4;
        }
    }
    return 1;  /* timeout */
}

/* ===================================================================
 *  Card initialization sequence
 * =================================================================== */

uint8_t sd_disk_initialize(void)
{
    uint32_t ocr;
    uint32_t count;
    uint8_t  validvoltage;
    uint8_t  err;
    uint8_t  i;
    uint32_t hcs = 0;  /* SD_STD_CAPACITY = 0 */

    /*
     * SDMMC pins (PC8, PC12, PD2) are on the VIO18/UHSIF domain.
     * They need 3.3V (MODE3) to communicate with the SD card.
     * WARNING: This conflicts with PSRAM on PE10-PE15 which needs 1.8V.
     * When using both, VIO18 must be switched before each access.
     */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR, ENABLE);
    PWR_VIO18ModeCfg(PWR_VIO18CFGMODE_SW);
    PWR_VIO18LevelCfg(PWR_VIO18Level_MODE3);

    /* Configure GPIO pins for SDMMC */
    sdmmc_gpio_init();

    /* Initialize SDMMC at slow clock for identification */
    sdmmc_hw_init_slow();

    card_type = SD_TYPE_SD1;  /* default to SD v1 */
    card_rca  = 0;

    /*
     * EVT: Send CMD0 up to 74 times as power-up clock sequence.
     * This also provides the 74+ clock cycles the card needs.
     */
    for (i = 0; i < 74; i++) {
        err = sdmmc_send_cmd(CMD0, 0, SDMMC_Resp_NONE);
        if (err == 0) break;
    }
    if (err) {
        disk_stat = STA_NOINIT;
        return SD_ERR_NO_CARD;
    }

    /* CMD8: SEND_IF_COND - voltage check */
    err = sdmmc_send_cmd(CMD8, 0x000001AA, SDMMC_Resp_48);
    if (err == 0) {
        /* SD v2.0 card detected */
        card_type = SD_TYPE_SD2;
        hcs = 0x40000000;  /* SD_HIGH_CAPACITY */
    }
    /* If CMD8 fails, remain as SD v1 (or MMC). Not fatal. */

    /*
     * EVT flow: Send CMD55 first to test if it's an SD card.
     * If CMD55 succeeds -> SD card, use ACMD41 loop.
     * If CMD55 fails -> MMC card, use CMD1 loop.
     */
    err = sdmmc_send_cmd(CMD55, 0, SDMMC_Resp_48);
    if (err == 0) {
        /* SD card path: ACMD41 loop */
        validvoltage = 0;
        count = 0;
        while (!validvoltage && count < 0xFFFF) {
            /* Must send CMD55 before every ACMD41 */
            err = sdmmc_send_cmd(CMD55, 0, SDMMC_Resp_48);
            if (err) {
                disk_stat = STA_NOINIT;
                return SD_ERR_ACMD41;
            }

            /* ACMD41: SD_VOLTAGE_WINDOW_SD | hcs */
            err = sdmmc_send_cmd(ACMD41, 0x80100000 | hcs, SDMMC_Resp_48);
            if (err) {
                disk_stat = STA_NOINIT;
                return SD_ERR_ACMD41;
            }

            /* EVT reads OCR from RESPONSE3 */
            ocr = SDMMC->RESPONSE3;
            validvoltage = ((ocr >> 31) == 1) ? 1 : 0;
            count++;
        }

        if (count >= 0xFFFF) {
            disk_stat = STA_NOINIT;
            return SD_ERR_ACMD41;
        }

        /* Check CCS bit for SDHC/SDXC */
        if (ocr & 0x40000000) {
            card_type = SD_TYPE_SD2 | SD_TYPE_BLOCK;
        }
    } else {
        /* MMC card path: CMD1 loop (not common, but handle it) */
        card_type = SD_TYPE_MMC;
        validvoltage = 0;
        count = 0;
        while (!validvoltage && count < 0xFFFF) {
            err = sdmmc_send_cmd(1, 0x80FF8000, SDMMC_Resp_48);
            if (err) {
                disk_stat = STA_NOINIT;
                return SD_ERR_ACMD41;
            }
            ocr = SDMMC->RESPONSE3;
            validvoltage = ((ocr >> 31) == 1) ? 1 : 0;
            count++;
        }
        if (count >= 0xFFFF) {
            disk_stat = STA_NOINIT;
            return SD_ERR_ACMD41;
        }
    }

    /* CMD2: ALL_SEND_CID */
    err = sd_cmd2();
    if (err) {
        disk_stat = STA_NOINIT;
        return SD_ERR_CMD2;
    }

    /* CMD3: SEND_RELATIVE_ADDR - get RCA */
    err = sd_cmd3();
    if (err) {
        disk_stat = STA_NOINIT;
        return SD_ERR_CMD3;
    }

    /* CMD9: SEND_CSD - read capacity */
    err = sd_cmd9();
    if (err) {
        disk_stat = STA_NOINIT;
        return SD_ERR_CMD9;
    }

    /* CMD7: SELECT_CARD - enter transfer state */
    err = sd_cmd7();
    if (err) {
        disk_stat = STA_NOINIT;
        return SD_ERR_CMD7;
    }

    /* Set block length to 512 for SDSC cards */
    if (!(card_type & SD_TYPE_BLOCK)) {
        sd_cmd16();
    }

    /* Stay in 1-bit bus mode.
     * 4-bit mode causes CRC errors on this V3F setup (under investigation).
     * 1-bit mode is confirmed working and sufficient for FatFS.
     * Set a moderate clock speed for reliable data transfers. */
    SDMMC_SetClockSpeed(SDMMC_ClockSpeed_Low, 0x08);

    disk_stat &= (DSTATUS)~STA_NOINIT;
    return SD_OK;
}

/* ===================================================================
 *  Disk read/write (called by FatFS diskio layer)
 * =================================================================== */

uint8_t sd_disk_read(uint8_t *buff, uint32_t sector, uint32_t count)
{
    if (disk_stat & STA_NOINIT) return 1;

    uint32_t addr = sector;
    if (!(card_type & SD_TYPE_BLOCK)) addr *= 512;

    /* Always read through aligned dma_rd_buf */
    while (count--) {
        if (sdmmc_read_block(dma_rd_buf, addr) != 0) return 1;
        memcpy(buff, dma_rd_buf, 512);
        buff += 512;
        addr += (card_type & SD_TYPE_BLOCK) ? 1 : 512;
    }
    return 0;
}

uint8_t sd_disk_write(const uint8_t *buff, uint32_t sector, uint32_t count)
{
    if (disk_stat & STA_NOINIT) return 1;

    uint32_t addr = sector;
    if (!(card_type & SD_TYPE_BLOCK)) addr *= 512;

    /* Always write through aligned dma_wr_buf */
    while (count--) {
        memcpy(dma_wr_buf, buff, 512);
        if (sdmmc_write_block(dma_wr_buf, addr) != 0) return 1;
        buff += 512;
        addr += (card_type & SD_TYPE_BLOCK) ? 1 : 512;
    }
    return 0;
}

/* ===================================================================
 *  Public API
 * =================================================================== */

uint8_t sd_init(void)
{
    uint8_t err = sd_disk_initialize();
    if (err != SD_OK) return err;

    /* Mount FatFS on drive 0 */
    FRESULT res = f_mount(&fs_obj, "0:", 1);
    if (res != FR_OK) {
        mounted = 0;
        return SD_ERR_MOUNT;
    }

    mounted = 1;
    return SD_OK;
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
    return sector_count;
}
