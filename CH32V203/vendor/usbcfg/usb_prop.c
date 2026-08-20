/*
 * Rovari User App - USB CDC Property Handlers
 * 1200 baud touch triggers bootloader re-entry.
 */

#include "usb_lib.h"
#include "usb_conf.h"
#include "usb_prop.h"
#include "usb_desc.h"
#include "usb_pwr.h"
#include "hw_config.h"

#define MAGIC_ADDR        ((volatile uint32_t *)0x20000400)
#define MAGIC_VALUE       0xDEADBEEF

uint8_t Request = 0;

extern uint8_t USBD_Endp3_Busy;

static uint8_t LineCoding[7] = {
    0x00, 0xC2, 0x01, 0x00,
    0x00,
    0x00,
    0x08
};

DEVICE Device_Table =
{
    EP_NUM,
    1
};

DEVICE_PROP Device_Property =
{
    USBD_init,
    USBD_Reset,
    USBD_Status_In,
    USBD_Status_Out,
    USBD_Data_Setup,
    USBD_NoData_Setup,
    USBD_Get_Interface_Setting,
    USBD_GetDeviceDescriptor,
    USBD_GetConfigDescriptor,
    USBD_GetStringDescriptor,
    0,
    DEF_USBD_UEP0_SIZE
};

USER_STANDARD_REQUESTS User_Standard_Requests =
{
    USBD_GetConfiguration,
    USBD_SetConfiguration,
    USBD_GetInterface,
    USBD_SetInterface,
    USBD_GetStatus,
    USBD_ClearFeature,
    USBD_SetEndPointFeature,
    USBD_SetDeviceFeature,
    USBD_SetDeviceAddress
};

ONE_DESCRIPTOR Device_Descriptor =
{
    (uint8_t*)USBD_DeviceDescriptor,
    USBD_SIZE_DEVICE_DESC
};

ONE_DESCRIPTOR Config_Descriptor =
{
    (uint8_t*)USBD_ConfigDescriptor,
    USBD_SIZE_CONFIG_DESC
};

ONE_DESCRIPTOR String_Descriptor[4] =
{
    {(uint8_t*)USBD_StringLangID, USBD_SIZE_STRING_LANGID},
    {(uint8_t*)USBD_StringVendor, USBD_SIZE_STRING_VENDOR},
    {(uint8_t*)USBD_StringProduct,USBD_SIZE_STRING_PRODUCT},
    {(uint8_t*)USBD_StringSerial, USBD_SIZE_STRING_SERIAL}
};

void USBD_SetConfiguration(void)
{
    DEVICE_INFO *pInfo = &Device_Info;
    if (pInfo->Current_Configuration != 0)
        bDeviceState = CONFIGURED;
}

void USBD_SetDeviceAddress(void)
{
    bDeviceState = ADDRESSED;
}

void USBD_SetDeviceFeature(void) {}
void USBD_ClearFeature(void) {}
void USBD_Status_Out(void) {}

void USBD_Status_In(void)
{
    uint32_t Request_No = pInformation->USBbRequest;

    if (Type_Recipient == (CLASS_REQUEST | INTERFACE_RECIPIENT))
    {
        if (Request_No == CDC_SET_LINE_CODING)
        {
            uint32_t baud = (uint32_t)LineCoding[0]
                          | (uint32_t)LineCoding[1] << 8
                          | (uint32_t)LineCoding[2] << 16
                          | (uint32_t)LineCoding[3] << 24;

            if (baud == 1200)
            {
                *MAGIC_ADDR = MAGIC_VALUE;
                NVIC_SystemReset();
            }
        }
    }
}

void USBD_init(void)
{
    uint8_t i;

    pInformation->Current_Configuration = 0;
    PowerOn();
    for (i=0;i<8;i++) _SetENDPOINT(i,_GetENDPOINT(i) & 0x7F7F & EPREG_MASK);
    _SetISTR((uint16_t)0x00FF);
    USB_SIL_Init();
    bDeviceState = UNCONNECTED;

    USB_Port_Set(DISABLE, DISABLE);
    Delay_Ms(20);
    USB_Port_Set(ENABLE, ENABLE);
}

void USBD_Reset(void)
{
    pInformation->Current_Configuration = 0;
    pInformation->Current_Feature = USBD_ConfigDescriptor[7];
    pInformation->Current_Interface = 0;

    SetBTABLE(BTABLE_ADDRESS);

    SetEPType(ENDP0, EP_CONTROL);
    SetEPTxStatus(ENDP0, EP_TX_STALL);
    SetEPRxAddr(ENDP0, ENDP0_RXADDR);
    SetEPTxAddr(ENDP0, ENDP0_TXADDR);
    Clear_Status_Out(ENDP0);
    SetEPRxCount(ENDP0, Device_Property.MaxPacketSize);
    SetEPRxValid(ENDP0);
    _ClearDTOG_RX(ENDP0);
    _ClearDTOG_TX(ENDP0);

    SetEPType(ENDP1, EP_INTERRUPT);
    SetEPTxStatus(ENDP1, EP_TX_NAK);
    SetEPTxAddr(ENDP1, ENDP1_TXADDR);
    SetEPRxStatus(ENDP1, EP_RX_DIS);
    _ClearDTOG_TX(ENDP1);
    _ClearDTOG_RX(ENDP1);

    SetEPType(ENDP2, EP_BULK);
    SetEPTxStatus(ENDP2, EP_TX_DIS);
    SetEPRxAddr(ENDP2, ENDP2_RXADDR);
    SetEPRxCount(ENDP2, DEF_USBD_MAX_PACK_SIZE);
    SetEPRxStatus(ENDP2, EP_RX_VALID);
    _ClearDTOG_RX(ENDP2);
    _ClearDTOG_TX(ENDP2);

    SetEPType(ENDP3, EP_BULK);
    SetEPTxStatus(ENDP3, EP_TX_NAK);
    SetEPTxAddr(ENDP3, ENDP3_TXADDR);
    SetEPRxStatus(ENDP3, EP_RX_DIS);
    _ClearDTOG_TX(ENDP3);
    _ClearDTOG_RX(ENDP3);

    SetDeviceAddress(0);

    USBD_Endp3_Busy = 0;

    bDeviceState = ATTACHED;
}

uint8_t *USBD_GetDeviceDescriptor(uint16_t Length)
{
    return Standard_GetDescriptorData(Length, &Device_Descriptor);
}

uint8_t *USBD_GetConfigDescriptor(uint16_t Length)
{
    return Standard_GetDescriptorData(Length, &Config_Descriptor);
}

uint8_t *USBD_GetStringDescriptor(uint16_t Length)
{
    uint8_t wValue0 = pInformation->USBwValue0;
    if (wValue0 > 4)
        return NULL;
    else
        return Standard_GetDescriptorData(Length, &String_Descriptor[wValue0]);
}

RESULT USBD_Get_Interface_Setting(uint8_t Interface, uint8_t AlternateSetting)
{
    if (AlternateSetting > 0)
        return USB_UNSUPPORT;
    else if (Interface > 1)
        return USB_UNSUPPORT;
    return USB_SUCCESS;
}

uint8_t *USB_CDC_GetLineCoding(uint16_t Length)
{
    if (Length == 0)
    {
        pInformation->Ctrl_Info.Usb_wLength = 7;
        return NULL;
    }
    return LineCoding;
}

uint8_t *USB_CDC_SetLineCoding(uint16_t Length)
{
    if (Length == 0)
    {
        pInformation->Ctrl_Info.Usb_wLength = 7;
        return NULL;
    }
    return LineCoding;
}

RESULT USBD_Data_Setup(uint8_t RequestNo)
{
    uint32_t Request_No = pInformation->USBbRequest;
    uint8_t *(*CopyRoutine)(uint16_t);
    CopyRoutine = NULL;

    if (Type_Recipient == (STANDARD_REQUEST | INTERFACE_RECIPIENT))
    {
        return USB_UNSUPPORT;
    }
    else if (Type_Recipient == (CLASS_REQUEST | INTERFACE_RECIPIENT))
    {
        if (Request_No == CDC_GET_LINE_CODING)
            CopyRoutine = &USB_CDC_GetLineCoding;
        else if (Request_No == CDC_SET_LINE_CODING)
            CopyRoutine = &USB_CDC_SetLineCoding;
        else
            return USB_UNSUPPORT;
    }

    if (CopyRoutine)
    {
        pInformation->Ctrl_Info.CopyData = CopyRoutine;
        pInformation->Ctrl_Info.Usb_wOffset = 0;
        (*CopyRoutine)(0);
    }
    else
    {
        return USB_UNSUPPORT;
    }
    return USB_SUCCESS;
}

static uint8_t dtr_was_set = 0;

RESULT USBD_NoData_Setup(uint8_t RequestNo)
{
    uint32_t Request_No = pInformation->USBbRequest;

    if (Type_Recipient == (CLASS_REQUEST | INTERFACE_RECIPIENT))
    {
        if (Request_No == CDC_SET_LINE_CTLSTE)
        {
            /* wValue bit 0 = DTR state */
            uint8_t dtr = pInformation->USBwValue0 & 0x01;

            if (dtr)
            {
                dtr_was_set = 1;
            }
            else if (dtr_was_set)
            {
                /* DTR went high then low = bootloader re-entry signal */
                *MAGIC_ADDR = MAGIC_VALUE;
                NVIC_SystemReset();
            }
        }
        else if (Request_No == CDC_SEND_BREAK)
        {
        }
        else
        {
            return USB_UNSUPPORT;
        }
    }
    return USB_SUCCESS;
}