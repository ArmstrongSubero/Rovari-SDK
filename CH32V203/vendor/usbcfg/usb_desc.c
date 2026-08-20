#include "usb_lib.h"
#include "usb_desc.h"

const uint8_t  USBD_DeviceDescriptor[] = {
    USBD_SIZE_DEVICE_DESC,
    0x01,
    0x10, 0x01,
    0x02,
    0x00,
    0x00,
    DEF_USBD_UEP0_SIZE,
    0x86, 0x1A,                     /* idVendor (WCH) */
    0x0C, 0xFE,                     /* idProduct */
    0x00, 0x01,
    0x01,
    0x02,
    0x03,
    0x01,
};

const uint8_t  USBD_ConfigDescriptor[] = {
    0x09, 0x02,
    USBD_SIZE_CONFIG_DESC & 0xFF, USBD_SIZE_CONFIG_DESC >> 8,
    0x02, 0x01, 0x00, 0x80, 0x32,

    /* Interface 0 (CDC) */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,

    /* Functional Descriptors */
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x01,
    0x04, 0x24, 0x02, 0x02,
    0x05, 0x24, 0x06, 0x00, 0x01,

    /* EP1 IN (Interrupt) */
    0x07, 0x05, 0x81, 0x03, 0x40, 0x00, 0x01,

    /* Interface 1 (Data) */
    0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,

    /* EP2 OUT (Bulk) */
    0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,

    /* EP3 IN (Bulk) */
    0x07, 0x05, 0x83, 0x02, 0x40, 0x00, 0x00,
};

const uint8_t USBD_StringLangID[USBD_SIZE_STRING_LANGID] = {
    USBD_SIZE_STRING_LANGID,
    USB_STRING_DESCRIPTOR_TYPE,
    0x09, 0x04
};

/* "Rovari" */
const uint8_t USBD_StringVendor[USBD_SIZE_STRING_VENDOR] = {
    USBD_SIZE_STRING_VENDOR,
    USB_STRING_DESCRIPTOR_TYPE,
    'R', 0, 'o', 0, 'v', 0, 'a', 0, 'r', 0, 'i', 0
};

/* "Rovari App" */
const uint8_t USBD_StringProduct[USBD_SIZE_STRING_PRODUCT] = {
    USBD_SIZE_STRING_PRODUCT,
    USB_STRING_DESCRIPTOR_TYPE,
    'R', 0, 'o', 0, 'v', 0, 'a', 0, 'r', 0, 'i', 0, ' ', 0,
    'A', 0, 'p', 0, 'p', 0
};

/* "0123456789" */
uint8_t USBD_StringSerial[USBD_SIZE_STRING_SERIAL] = {
    USBD_SIZE_STRING_SERIAL,
    USB_STRING_DESCRIPTOR_TYPE,
    '0', 0, '1', 0, '2', 0, '3', 0, '4', 0, '5', 0, '6', 0, '7', 0, '8', 0, '9', 0
};