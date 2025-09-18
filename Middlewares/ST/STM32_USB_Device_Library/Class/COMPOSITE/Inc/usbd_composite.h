#ifndef USBD_COMPOSITE_H
#define USBD_COMPOSITE_H
#include "usbd_def.h"
#include "usbd_cdc.h"
#include "usbd_msc.h"
#include "usbd_audio.h"




extern USBD_ClassTypeDef USBD_CMPSIT;
extern USBD_DescriptorsTypeDef usbCmpsitFS_Desc;
#ifdef USE_USBD_COMPOSITE
void USBD_CMPSIT_AddClass(USBD_HandleTypeDef *pdev, USBD_ClassTypeDef *pclass, USBD_CompositeClassTypeDef classtype, uint8_t *EpAddr);
uint8_t USBD_get_composite_class_id(USBD_HandleTypeDef *pdev, uint8_t classType);
#endif
#endif