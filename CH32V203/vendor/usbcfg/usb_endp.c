#include "usb_lib.h"
#include "usb_desc.h"
#include "usb_mem.h"
#include "hw_config.h"
#include "usb_istr.h"
#include "usb_pwr.h"
#include "usb_prop.h"

uint8_t USBD_Endp3_Busy;
static uint8_t rx_buf[64];

#define MAGIC_ADDR        ((volatile uint32_t *)0x20000400)
#define MAGIC_VALUE       0xDEADBEEF

void EP1_IN_Callback(void) { }

void EP2_OUT_Callback(void)
{
    uint16_t len = GetEPRxCount(EP2_OUT & 0x7F);
    PMAToUserBufferCopy(rx_buf, GetEPRxAddr(EP2_OUT & 0x7F), len);
    SetEPRxValid(ENDP2);

    /* Check for "ROVBOOT" reset command (7 bytes) */
    if (len >= 7 &&
        rx_buf[0] == 'R' && rx_buf[1] == 'O' && rx_buf[2] == 'V' &&
        rx_buf[3] == 'B' && rx_buf[4] == 'O' && rx_buf[5] == 'O' &&
        rx_buf[6] == 'T')
    {
        *MAGIC_ADDR = MAGIC_VALUE;
        NVIC_SystemReset();
    }
}

void EP3_IN_Callback(void)
{
    USBD_Endp3_Busy = 0;
}

uint8_t USBD_ENDPx_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len)
{
    if (endp == ENDP3)
    {
        if (USBD_Endp3_Busy) return USB_ERROR;
        USB_SIL_Write(EP3_IN, pbuf, len);
        USBD_Endp3_Busy = 1;
        SetEPTxStatus(ENDP3, EP_TX_VALID);
    }
    else return USB_ERROR;
    return USB_SUCCESS;
}