/**
  ******************************************************************************
  * @file    usbd_midi.h
  * @author  MCD Application Team
  * @brief   header file for the usbd_midi.c file.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USBD_MIDI_H
#define __USBD_MIDI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include  "usbd_ioreq.h"

/** @addtogroup STM32_USB_DEVICE_LIBRARY
  * @{
  */

/** @defgroup USBD_MIDI
  * @brief This file is the Header file for USBD_customhid.c
  * @{
  */


/** @defgroup USBD_MIDI_Exported_Defines
  * @{
  */
#ifndef MIDI_EPIN_ADDR
#define MIDI_EPIN_ADDR                         0x81U
#endif /* MIDI_EPIN_ADDR */

#ifndef MIDI_EPIN_SIZE
#define MIDI_EPIN_SIZE                         0x02U
#endif /* MIDI_EPIN_SIZE */

#ifndef MIDI_EPOUT_ADDR
#define MIDI_EPOUT_ADDR                        0x01U
#endif /* MIDI_EPOUT_ADDR */

#ifndef MIDI_EPOUT_SIZE
#define MIDI_EPOUT_SIZE                        0x02U
#endif /* MIDI_EPOUT_SIZE*/

#define USB_MIDI_CONFIG_DESC_SIZ               101U
#define USB_MIDI_DESC_SIZ                      9U

#ifndef MIDI_HS_BINTERVAL
#define MIDI_HS_BINTERVAL                      0x05U
#endif /* MIDI_HS_BINTERVAL */

#ifndef MIDI_FS_BINTERVAL
#define MIDI_FS_BINTERVAL                      0x05U
#endif /* MIDI_FS_BINTERVAL */

#ifndef USBD_MIDI_OUTREPORT_BUF_SIZE
#define USBD_MIDI_OUTREPORT_BUF_SIZE            0x02U
#endif /* USBD_MIDI_OUTREPORT_BUF_SIZE */

#ifndef USBD_MIDI_REPORT_DESC_SIZE
#define USBD_MIDI_REPORT_DESC_SIZE             163U
#endif /* USBD_MIDI_REPORT_DESC_SIZE */

#define MIDI_DESCRIPTOR_TYPE                   0x21U
#define MIDI_REPORT_DESC                       0x22U

#define MIDI_REQ_SET_PROTOCOL                  0x0BU
#define MIDI_REQ_GET_PROTOCOL                  0x03U

#define MIDI_REQ_SET_IDLE                      0x0AU
#define MIDI_REQ_GET_IDLE                      0x02U

#define MIDI_REQ_SET_REPORT                    0x09U
#define MIDI_REQ_GET_REPORT                    0x01U

/*bDescriptorSubType*/
#define MIDI_STREAMING_HEADER   0x01U 
#define MIDI_STREAMING_IN_JACK  0x02U
#define MIDI_STREAMING_OUT_JACK 0x03U

#define MIDI_STREAMING_GENERAL 0x01U

#define MIDI_JACK_TYPE_EMBEDDED                0x01U
#define MIDI_JACK_TYPE_EXTERNAL                0x02U

/**
  * @}
  */


/** @defgroup USBD_CORE_Exported_TypesDefinitions
  * @{
  */
typedef enum
{
  MIDI_IDLE = 0U,
  MIDI_BUSY,
} MIDI_StateTypeDef;

typedef struct _USBD_MIDI_Itf
{
  uint8_t *pReport;
  int8_t (* Init)(void);
  int8_t (* DeInit)(void);
  int8_t (* OutEvent)(uint8_t event_idx, uint8_t state);
#ifdef USBD_MIDI_CTRL_REQ_COMPLETE_CALLBACK_ENABLED
  int8_t (* CtrlReqComplete)(uint8_t request, uint16_t wLength);
#endif /* USBD_MIDI_CTRL_REQ_COMPLETE_CALLBACK_ENABLED */
#ifdef USBD_MIDI_CTRL_REQ_GET_REPORT_ENABLED
  uint8_t *(* GetReport)(uint16_t *ReportLength);
#endif /* USBD_MIDI_CTRL_REQ_GET_REPORT_ENABLED */
} USBD_MIDI_ItfTypeDef;

typedef struct
{
  uint8_t  Report_buf[USBD_MIDI_OUTREPORT_BUF_SIZE];
  uint32_t Protocol;
  uint32_t IdleState;
  uint32_t AltSetting;
  uint32_t IsReportAvailable;
  MIDI_StateTypeDef state;
} USBD_MIDI_HandleTypeDef;

/*
 * HID Class specification version 1.1
 * 6.2.1 HID Descriptor
 */

typedef struct
{
  uint8_t           bLength;
  uint8_t           bDescriptorTypeCHID;
  uint16_t          bcdMIDI;
  uint8_t           bCountryCode;
  uint8_t           bNumDescriptors;
  uint8_t           bDescriptorType;
  uint16_t          wItemLength;
} __PACKED USBD_DescTypeDef;

/**
  * @}
  */



/** @defgroup USBD_CORE_Exported_Macros
  * @{
  */

/**
  * @}
  */

/** @defgroup USBD_CORE_Exported_Variables
  * @{
  */

extern USBD_ClassTypeDef USBD_MIDI;
#define USBD_MIDI_CLASS &USBD_MIDI
/**
  * @}
  */

/** @defgroup USB_CORE_Exported_Functions
  * @{
  */
#ifdef USE_USBD_COMPOSITE
uint8_t USBD_MIDI_SendReport(USBD_HandleTypeDef *pdev,
                                   uint8_t *report, uint16_t len, uint8_t ClassId);
#else
uint8_t USBD_MIDI_SendReport(USBD_HandleTypeDef *pdev,
                                   uint8_t *report, uint16_t len);
#endif /* USE_USBD_COMPOSITE */
uint8_t USBD_MIDI_ReceivePacket(USBD_HandleTypeDef *pdev);

uint8_t USBD_MIDI_RegisterInterface(USBD_HandleTypeDef *pdev,
                                          USBD_MIDI_ItfTypeDef *fops);

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif  /* __USBD_MIDI_H */
/**
  * @}
  */

/**
  * @}
  */

