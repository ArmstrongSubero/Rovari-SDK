#include "ch32v00x.h"

extern GPIO_TypeDef* SD_CD_PORT; //CD port (GPIOx)
extern uint16_t      SD_CD_PIN; //CD pin (GPIO_Pin_x)

void SPI1_Init(void);
void SPI1_SetSpeed(uint8_t SPI_BaudRatePrescaler);
uint8_t   SPI_TransferByte(uint8_t TxData);
