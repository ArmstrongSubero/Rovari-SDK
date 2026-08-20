/*
 * sd.c - drop-in replacement keeping the Part-12 API, but using the init /
 * read / write that actually cleared ACMD41 and read the BPB on this card:
 *   - one flat command sender, CS held LOW for the whole command
 *   - no per-command SD_WaitReady gating, no transfer bailout
 *   - byte vs block addressing from the card type
 * Pairs with the mode-3 spi.c. diskio.c and main.c are unchanged.
 */
#include "sd.h"
#include "spi.h"
#include "debug.h"
#include "ch32v00x_gpio.h"

uint8_t SD_Type = 0;
GPIO_TypeDef* SD_CD_PORT;
uint16_t      SD_CD_PIN;

#define xfer(b)  SPI_TransferByte(b)

void SD_LowSpeed(void)  { SPI1_SetSpeed(SPI_BaudRatePrescaler_256); }
void SD_HighSpeed(void) { SPI1_SetSpeed(SPI_BaudRatePrescaler_8);   }

uint8_t SD_Detect(GPIO_TypeDef* GPIOx, uint16_t pin)
{
    return (GPIO_ReadInputDataBit(GPIOx, pin) == Bit_RESET);
}

void SD_SetChipDetect(GPIO_TypeDef* port, uint16_t pin)
{
    GPIO_InitTypeDef g = {0};
    if (port == GPIOA) RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    else if (port == GPIOC) RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    else if (port == GPIOD) RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    g.GPIO_Pin = pin; g.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(port, &g);
    SD_CD_PORT = port; SD_CD_PIN = pin;
}

void SD_SPI_Init(void) { SPI1_Init(); SD_ChipSelect_High; }

void SD_Deselect(void) { SD_ChipSelect_High; xfer(0xFF); }

uint8_t SD_WaitReady(void)
{
    uint32_t t = 0;
    do { if (xfer(0xFF) == 0xFF) return 0; t++; } while (t < 0x40000);
    return 1;
}

uint8_t SD_GetResponse(uint8_t response)
{
    uint16_t cnt = 0xFFFF;
    while ((xfer(0xFF) != response) && cnt) cnt--;
    return cnt ? MSD_RESPONSE_NO_ERROR : MSD_RESPONSE_FAILURE;
}

/* ---- flat command: CS held LOW for the whole command, returns R1 ---- */
static uint8_t sd_cmd(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    uint8_t r1, n;
    SD_ChipSelect_High; xfer(0xFF);     /* re-sync */
    SD_ChipSelect_Low;  xfer(0xFF);
    xfer(cmd | 0x40);
    xfer((uint8_t)(arg >> 24));
    xfer((uint8_t)(arg >> 16));
    xfer((uint8_t)(arg >> 8));
    xfer((uint8_t)arg);
    xfer(crc);
    n = 20; do { r1 = xfer(0xFF); } while ((r1 & 0x80) && --n);
    return r1;     /* leaves CS LOW */
}

uint8_t SD_ReceiveData(uint8_t *buffer, uint16_t length)
{
    if (SD_GetResponse(0xFE)) return 1;
    for (u16 i = 0; i < length; i++) buffer[i] = xfer(0xFF);
    xfer(0xFF); xfer(0xFF);
    return 0;
}

uint8_t SD_SendBlock(uint8_t *buffer, uint8_t token)
{
    uint16_t resp;
    if (SD_WaitReady()) return 1;
    xfer(token);
    for (u16 i = 0; i < 512; i++) xfer(buffer[i]);
    xfer(0xFF); xfer(0xFF);
    resp = xfer(0xFF);
    if ((resp & 0x1F) != 0x05) return 2;
    if (SD_WaitReady()) return 3;       /* wait out programming busy */
    return 0;
}

uint8_t SD_GetCID(uint8_t *cid)
{
    uint8_t r1 = sd_cmd(CMD10, 0, 0x01);
    if (r1 == 0) r1 = SD_ReceiveData(cid, 16);
    SD_Deselect();
    return r1 ? 1 : 0;
}

uint8_t SD_GetCSD(uint8_t *csd)
{
    uint8_t r1 = sd_cmd(CMD9, 0, 0x01);
    if (r1 == 0) r1 = SD_ReceiveData(csd, 16);
    SD_Deselect();
    return r1 ? 1 : 0;
}

uint32_t SD_GetSectorCount(void)
{
    uint8_t csd[16], n; uint16_t csize; uint32_t cap;
    if (SD_GetCSD(csd) != 0) return 0;
    if ((csd[0] & 0xC0) == 0x40) {           /* CSD v2 (SDHC) */
        csize = csd[9] + ((uint16_t)csd[8] << 8) + 1;
        cap = (uint32_t)csize << 10;
    } else {                                 /* CSD v1 (SDSC) */
        n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
        csize = (csd[8] >> 6) + ((uint16_t)csd[7] << 2) + ((uint16_t)(csd[6] & 3) << 10) + 1;
        cap = (uint32_t)csize << (n - 9);
    }
    return cap;
}

uint8_t SD_Initialize(void)
{
    uint8_t r1, buf[4]; uint16_t i; uint8_t v2 = 0;
    uint16_t retry;

    SD_SPI_Init();
    SD_LowSpeed();

    SD_ChipSelect_High;
    for (i = 0; i < 10; i++) xfer(0xFF);     /* 80 clocks */

    /* CMD0 */
    retry = 100;
    do { r1 = sd_cmd(CMD0, 0, 0x95); } while (r1 != 0x01 && --retry);
    SD_Deselect();
    if (r1 != 0x01) return r1;

    /* CMD8 */
    r1 = sd_cmd(CMD8, 0x1AA, 0x87);
    if (r1 == 0x01) {
        for (i = 0; i < 4; i++) buf[i] = xfer(0xFF);
        if (buf[2] == 0x01 && buf[3] == 0xAA) v2 = 1;
    }
    SD_Deselect();

    SD_Type = 0;

    /* ACMD41: CMD55 + CMD41 each as its own flat command, HCS set for v2 */
    retry = 0xFFFF;
    do {
        sd_cmd(CMD55, 0, 0x01);
        r1 = sd_cmd(CMD41, v2 ? 0x40000000UL : 0, 0x01);
        SD_Deselect();
        if (r1 == 0x00) break;
        Delay_Ms(1);
    } while (--retry);

    if (r1 != 0x00) {                         /* MMC fallback */
        retry = 0xFFFF;
        do { r1 = sd_cmd(CMD1, 0, 0x01); SD_Deselect(); if (!r1) break; Delay_Ms(1); } while (--retry);
        if (r1 == 0x00) SD_Type = SD_TYPE_MMC;
    } else {
        SD_Type = v2 ? SD_TYPE_V2 : SD_TYPE_V1;
    }

    if (SD_Type == 0) { SD_Deselect(); return (r1 ? r1 : 0xAA); }

    /* CMD58: read OCR, detect block addressing */
    if (v2) {
        r1 = sd_cmd(CMD58, 0, 0x01);
        if (r1 == 0x00) {
            for (i = 0; i < 4; i++) buf[i] = xfer(0xFF);
            if (buf[0] & 0x40) SD_Type = SD_TYPE_V2HC;
        }
        SD_Deselect();
    }

    /* set block length 512 for byte-addressed cards */
    if (SD_Type != SD_TYPE_V2HC) { sd_cmd(CMD16, 512, 0x01); SD_Deselect(); }

    return 0;
}

uint8_t SD_ReadDisk(uint8_t *buffer, uint32_t sector, uint8_t count)
{
    uint8_t r1;
    if (SD_Type != SD_TYPE_V2HC) sector <<= 9;   /* byte address for SDSC */

    if (count == 1) {
        r1 = sd_cmd(CMD17, sector, 0x01);
        if (r1 == 0) r1 = SD_ReceiveData(buffer, 512);
    } else {
        r1 = sd_cmd(CMD18, sector, 0x01);
        if (r1 == 0) {
            do { r1 = SD_ReceiveData(buffer, 512); buffer += 512; } while (--count && r1 == 0);
            sd_cmd(CMD12, 0, 0x01);
        }
    }
    SD_Deselect();
    return r1;
}

uint8_t SD_WriteDisk(uint8_t *buffer, uint32_t sector, uint8_t cnt)
{
    uint8_t r1;
    if (SD_Type != SD_TYPE_V2HC) sector <<= 9;   /* byte address for SDSC */

    if (cnt == 1) {
        r1 = sd_cmd(CMD24, sector, 0x01);
        if (r1 == 0) {
            xfer(0xFF);                 /* gap */
            r1 = SD_SendBlock(buffer, 0xFE);
        }
    } else {
        if (SD_Type != SD_TYPE_MMC) { sd_cmd(CMD55, 0, 0x01); sd_cmd(CMD23, cnt, 0x01); }
        r1 = sd_cmd(CMD25, sector, 0x01);
        if (r1 == 0) {
            xfer(0xFF);
            do { r1 = SD_SendBlock(buffer, 0xFC); buffer += 512; } while (--cnt && r1 == 0);
            xfer(0xFD);                 /* stop token */
            SD_WaitReady();
        }
    }
    SD_Deselect();
    return r1;
}