#ifndef USBD_COMPOSITE_H
#define USBD_COMPOSITE_H
#include "usbd_def.h"
#include "usbd_cdc.h"
#include "usbd_msc.h"
#include "usbd_audio.h"
#include "usbd_conf.h"


#define  USBD_IDX_CDC_INTF_STR                    0x06
#define  USBD_IDX_MSC_INTF_STR                    0x07
#define  USBD_IDX_AUDIO_INTF_STR                  0x08


/* Derived counts and sizes */
#define USBD_CMPSIT_NUM_INTERFACES   (USBD_AUDIO_INTERFACES_NUM + USBD_MSC_INTERFACES_NUM + USBD_CDC_INTERFACES_NUM)

#define USB_CMPSIT_CONFIG_DESC_SIZ   (9 + USB_CMPSIT_AUDIO_CONFIG_DESC_SIZ + USB_CMPSIT_CDC_CONFIG_DESC_SIZ + USB_CMPSIT_MSC_CONFIG_DESC_SIZ)

#define CDC_COMM_ITF_NBR 0x00U
#define CDC_DATA_ITF_NBR 0x01U
#define MSC_STD_ITF_NBR 0x02U
#define AUDIO_SPKR_AC_ITF_NBR 0x03U
#define AUDIO_SPKR_AS_ITF_NBR 0x04U
#define AUDIO_SPKR_STR_DESC_IDX 0x00U
#define AUDIO_STREAMING_CTRL                          0x02U


extern USBD_ClassTypeDef USBD_CMPSIT;
extern USBD_DescriptorsTypeDef usbCmpsitFS_Desc;
#ifdef USE_USBD_COMPOSITE
void USBD_CMPSIT_AddClass(USBD_HandleTypeDef *pdev, USBD_ClassTypeDef *pclass, USBD_CompositeClassTypeDef classtype, uint8_t *EpAddr);
uint8_t USBD_get_composite_class_id(USBD_HandleTypeDef *pdev, uint8_t classType);
#endif
#endif