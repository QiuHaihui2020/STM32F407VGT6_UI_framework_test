/**
  ******************************************************************************
  * @file    usbd_midi.c
  * @author  MCD Application Team
  * @brief   This file provides the MIDI core functions.
  *
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
  * @verbatim
  *
  *          ===================================================================
  *                                MIDI Class  Description
  *          ===================================================================
  *           This module manages the MIDI class V1.11 following the "Device Class Definition
  *           for Human Interface Devices (MIDI) Version 1.11 Jun 27, 2001".
  *           This driver implements the following aspects of the specification:
  *             - The Boot Interface Subclass
  *             - Usage Page : Generic Desktop
  *             - Usage : Vendor
  *             - Collection : Application
  *
  * @note     In HS mode and when the DMA is used, all variables and data structures
  *           dealing with the DMA during the transaction process should be 32-bit aligned.
  *
  *
  *  @endverbatim
  *
  ******************************************************************************
  */

/* BSPDependencies
- "stm32xxxxx_{eval}{discovery}{nucleo_144}.c"
- "stm32xxxxx_{eval}{discovery}_io.c"
EndBSPDependencies */

/* Includes ------------------------------------------------------------------*/
#include "usbd_midi.h"
#include "usbd_ctlreq.h"
#include "usbd_audio.h"


/** @addtogroup STM32_USB_DEVICE_LIBRARY
  * @{
  */


/** @defgroup USBD_MIDI
  * @brief usbd core module
  * @{
  */

/** @defgroup USBD_MIDI_Private_TypesDefinitions
  * @{
  */
/**
  * @}
  */


/** @defgroup USBD_MIDI_Private_Defines
  * @{
  */

/**
  * @}
  */


/** @defgroup USBD_MIDI_Private_Macros
  * @{
  */
/**
  * @}
  */
/** @defgroup USBD_MIDI_Private_FunctionPrototypes
  * @{
  */

static uint8_t USBD_MIDI_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_MIDI_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_MIDI_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);

static uint8_t USBD_MIDI_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_MIDI_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_MIDI_EP0_RxReady(USBD_HandleTypeDef  *pdev);
#ifndef USE_USBD_COMPOSITE
static uint8_t *USBD_MIDI_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_MIDI_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_MIDI_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t *USBD_MIDI_GetDeviceQualifierDesc(uint16_t *length);
#endif /* USE_USBD_COMPOSITE  */
/**
  * @}
  */

/** @defgroup USBD_MIDI_Private_Variables
  * @{
  */

USBD_ClassTypeDef  USBD_MIDI =
{
  USBD_MIDI_Init,
  USBD_MIDI_DeInit,
  USBD_MIDI_Setup,
  NULL, /*EP0_TxSent*/
  USBD_MIDI_EP0_RxReady, /*EP0_RxReady*/ /* STATUS STAGE IN */
  USBD_MIDI_DataIn, /*DataIn*/
  USBD_MIDI_DataOut,
  NULL, /*SOF */
  NULL,
  NULL,
#ifdef USE_USBD_COMPOSITE
  NULL,
  NULL,
  NULL,
  NULL,
#else
  USBD_MIDI_GetHSCfgDesc,
  USBD_MIDI_GetFSCfgDesc,
  USBD_MIDI_GetOtherSpeedCfgDesc,
  USBD_MIDI_GetDeviceQualifierDesc,
#endif /* USE_USBD_COMPOSITE  */
};

#define USBD_MIDI_INTERFACES_NUM    2
#define MIDI_AC_ITF_NBR 0x00U
#define MIDI_MS_ITF_NBR 0x01U

#ifndef USE_USBD_COMPOSITE
/* USB MIDI device FS Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_MIDI_CfgDesc[USB_MIDI_CONFIG_DESC_SIZ] __ALIGN_END =
{
  0x09,                                       /* bLength: Configuration Descriptor size */
  USB_DESC_TYPE_CONFIGURATION,                /* bDescriptorType: Configuration */
  LOBYTE(USB_MIDI_CONFIG_DESC_SIZ),                    /* wTotalLength */
  HIBYTE(USB_MIDI_CONFIG_DESC_SIZ),
  USBD_MIDI_INTERFACES_NUM,                                       /* bNumInterfaces: 2 interfaces */
  0x01,                                       /* bConfigurationValue: Configuration value */
  0x00,                                       /* iConfiguration: Index of string descriptor
                                                 describing the configuration */
#if (USBD_SELF_POWERED == 1U)
  0xC0,                                       /* bmAttributes: Bus Powered according to user configuration */
#else
  0x80,                                       /* bmAttributes: Bus Powered according to user configuration */
#endif /* USBD_SELF_POWERED */
  USBD_MAX_POWER,                             /* MaxPower (mA) */

/******************* Standard AC Interface Descriptor *********************/
/* 09 */
0x09,         /*bLength: Interface Descriptor size*/
USB_DESC_TYPE_INTERFACE, /*bDescriptorType: Interface descriptor type*/
MIDI_AC_ITF_NBR,         /*bInterfaceNumber: Number of Interface*/
0x00,         /*bAlternateSetting: Alternate setting*/
0x00,         /*bNumEndpoints*/
USB_DEVICE_CLASS_AUDIO,         /*bInterfaceClass: Audio*/
AUDIO_SUBCLASS_AUDIOCONTROL,         /*bInterfaceSubClass : Audio Control*/
AUDIO_PROTOCOL_UNDEFINED,            /*nInterfaceProtocol*/
0x00,            /*iInterface: Index of string descriptor*/

/**************** Class-specific AC Interface Descriptor ******************/
/* 18 */
0x09,         /*bLength: Interface Descriptor size*/
AUDIO_INTERFACE_DESCRIPTOR_TYPE,         /*bDescriptorType: Class-specific interface descriptor type*/
AUDIO_CONTROL_HEADER,         /*bDescriptorSubType: Header*/
0x00,         /*bcdADC: Revision of class specification - 1.0*/
0x01,
0x09,         /*wTotalLength: Total size of class specific discriptor*/
0x00,
0x01,         /*bInCollection: Number of streaming interfaces*/
MIDI_MS_ITF_NBR,         /*baInterfaceNr : MIDIStreaming interface 1 belongs to this AudioControl interface*/

/******************* Standard MS Interface Descriptor *********************/
/* 27 */
0x09,         /*bLength: Interface Descriptor size*/
USB_DESC_TYPE_INTERFACE, /*bDescriptorType: Interface descriptor type*/
MIDI_MS_ITF_NBR,         /*bInterfaceNumber: Number of Interface*/
0x00,         /*bAlternateSetting: Alternate setting*/
0x02,         /*bNumEndpoints*/
USB_DEVICE_CLASS_AUDIO,         /*bInterfaceClass: Audio*/
AUDIO_SUBCLASS_MIDISTREAMING,         /*bInterfaceSubClass : MIDI Streaming*/
AUDIO_PROTOCOL_UNDEFINED,            /*nInterfaceProtocol*/
0x00,            /*iInterface: Index of string descriptor*/

/**************** Class-specific MS Interface Descriptor ******************/
/* 36 */
0x07,         /*bLength: Interface Descriptor size*/
AUDIO_INTERFACE_DESCRIPTOR_TYPE,         /*bDescriptorType: Class-specific interface descriptor type*/
MIDI_STREAMING_HEADER,         /*bDescriptorSubType: MS Header*/
0x00,         /*bcdADC: Revision of class specification*/
0x01,
0x41,         /*wTotalLength: Total size of class specific discriptor*/
0x00,

/******************* MIDI IN Jack Descriptor (Embedded) *******************/
/* 43 */
0x06,         /*bLength: Size of this descriptor*/
AUDIO_INTERFACE_DESCRIPTOR_TYPE,         /*bDescriptorType: Class-specific interface descriptor type*/
MIDI_STREAMING_IN_JACK,         /*bDescriptorSubType: MIDI IN Jack*/
MIDI_JACK_TYPE_EMBEDDED,         /*bJackType: Embedded*/
0x01,         /*bJackID: ID of this Jack*/
0x00,         /*iJack*/

/******************* MIDI IN Jack Descriptor (External) *******************/
/* 49 */
0x06,         /*bLength: Size of this descriptor*/
AUDIO_INTERFACE_DESCRIPTOR_TYPE,         /*bDescriptorType: Class-specific interface descriptor type*/
MIDI_STREAMING_IN_JACK,         /*bDescriptorSubType: MIDI IN Jack*/
MIDI_JACK_TYPE_EXTERNAL,         /*bJackType: External*/
0x02,         /*bJackID: ID of this Jack*/
0x00,         /*iJack*/

/******************* MIDI OUT Jack Descriptor (Embedded) ******************/
/* 55 */
0x09,         /*bLength: Size of this descriptor*/
AUDIO_INTERFACE_DESCRIPTOR_TYPE,         /*bDescriptorType: Class-specific interface descriptor type*/
MIDI_STREAMING_OUT_JACK,         /*bDescriptorSubType: MIDI OUT Jack*/
MIDI_JACK_TYPE_EMBEDDED,         /*bJackType: Embedded*/
0x03,         /*bJackID: ID of this Jack*/
0x01,         /*bNrInputPins: Number of Input Pins of this Jack*/
0x02,         /*BaSourceID: ID of the Entry to which this Pin is connected*/
0x01,         /*BaSourceID: Output Pin number of the Entry to which this Input Pin is connected*/
0x00,         /*iJack*/

/******************* MIDI OUT Jack Descriptor (External) ******************/
/* 64 */
0x09,         /*bLength: Size of this descriptor*/
AUDIO_INTERFACE_DESCRIPTOR_TYPE,         /*bDescriptorType: Class-specific interface descriptor type*/
MIDI_STREAMING_OUT_JACK,         /*bDescriptorSubType: MIDI OUT Jack*/
MIDI_JACK_TYPE_EXTERNAL,         /*bJackType: External*/
0x04,         /*bJackID: ID of this Jack*/
0x01,         /*bNrInputPins: Number of Input Pins of this Jack*/
0x01,         /*BaSourceID: ID of the Entry to which this Pin is connected*/
0x01,         /*BaSourceID: Output Pin number of the Entry to which this Input Pin is connected*/
0x00,         /*iJack*/

/****************** Standard Bulk OUT Endpoint Descriptor *****************/
/* 73 */
0x09,         /*bLength: Size of this descriptor*/
USB_DESC_TYPE_ENDPOINT, /*bDescriptorType: Endpoint descriptor type*/
MIDI_EPOUT_ADDR,         /*bEndpointAddress: OUT Endpoint 1*/
USBD_EP_TYPE_BULK,         /*bmAttributes: Bulk, not shared.*/
0x40,         /*wMaxPacketSize 64*/
0x00,
0x00,         /*bInterval*/
0x00,         /*bRefresh*/
0x00,         /*bSynchAddress*/

/************* Class-specific MS Bulk OUT Endpoint Descriptor *************/
/* 82 */
0x05,         /*bLength: Size of this descriptor*/
AUDIO_ENDPOINT_DESCRIPTOR_TYPE,         /*bDescriptorType: Class-specific endpoint descriptor type*/
MIDI_STREAMING_GENERAL,         /*bDescriptorSubType: MS General*/
0x01,         /*bNumEmbMIDIJack: Number of embedded MIDI IN Jack*/
0x01,         /*BaAssocJackID: ID of the Embedded MIDI IN Jack*/

/****************** Standard Bulk IN Endpoint Descriptor *****************/
/* 87 */
0x09,         /*bLength: Size of this descriptor*/
USB_DESC_TYPE_ENDPOINT, /*bDescriptorType: Endpoint descriptor type*/
MIDI_EPIN_ADDR,         /*bEndpointAddress: IN Endpoint 1*/
USBD_EP_TYPE_BULK,         /*bmAttributes: Bulk, not shared.*/
0x40,         /*wMaxPacketSize 64*/
0x00,
0x00,         /*bInterval*/
0x00,         /*bRefresh*/
0x00,         /*bSynchAddress*/
/************* Class-specific MS Bulk OUT Endpoint Descriptor *************/
/* 96 */

0x05,         /*bLength: Size of this descriptor*/
AUDIO_ENDPOINT_DESCRIPTOR_TYPE,         /*bDescriptorType: Class-specific endpoint descriptor type*/
MIDI_STREAMING_GENERAL,         /*bDescriptorSubType: MS General*/
0x01,         /*bNumEmbMIDIJack: Number of embedded MIDI OUT Jack*/
0x03,         /*BaAssocJackID: ID of the Embedded MIDI OUT Jack*/
/* 101 */
};
#endif /* USE_USBD_COMPOSITE  */

/* USB MIDI device Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_MIDI_Desc[USB_MIDI_DESC_SIZ] __ALIGN_END =
{
  /* 18 */
  0x09,                                               /* bLength: MIDI Descriptor size */
  MIDI_DESCRIPTOR_TYPE,                         /* bDescriptorType: MIDI */
  0x11,                                               /* bMIDIUSTOM_HID: MIDI Class Spec release number */
  0x01,
  0x00,                                               /* bCountryCode: Hardware target country */
  0x01,                                               /* bNumDescriptors: Number of MIDI class descriptors
                                                         to follow */
  0x22,                                               /* bDescriptorType */
  LOBYTE(USBD_MIDI_REPORT_DESC_SIZE),                   /* wItemLength: Total length of Report descriptor */
  HIBYTE(USBD_MIDI_REPORT_DESC_SIZE),
};

#ifndef USE_USBD_COMPOSITE
/* USB Standard Device Descriptor */
__ALIGN_BEGIN static uint8_t USBD_MIDI_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00,
  0x02,
  0x00,
  0x00,
  0x00,
  0x40,
  0x01,
  0x00,
};
#endif /* USE_USBD_COMPOSITE  */

static uint8_t MIDIInEpAdd = MIDI_EPIN_ADDR;
static uint8_t MIDIOutEpAdd = MIDI_EPOUT_ADDR;
/**
  * @}
  */

/** @defgroup USBD_MIDI_Private_Functions
  * @{
  */

/**
  * @brief  USBD_MIDI_Init
  *         Initialize the MIDI interface
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t USBD_MIDI_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);
  USBD_MIDI_HandleTypeDef *hhid;

  hhid = (USBD_MIDI_HandleTypeDef *)USBD_malloc(sizeof(USBD_MIDI_HandleTypeDef));

  if (hhid == NULL)
  {
    pdev->pClassDataCmsit[pdev->classId] = NULL;
    return (uint8_t)USBD_EMEM;
  }

  pdev->pClassDataCmsit[pdev->classId] = (void *)hhid;
  pdev->pClassData = pdev->pClassDataCmsit[pdev->classId];

#ifdef USE_USBD_COMPOSITE
  /* Get the Endpoints addresses allocated for this class instance */
  MIDIInEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
  MIDIOutEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

  if (pdev->dev_speed == USBD_SPEED_HIGH)
  {
    pdev->ep_in[MIDIInEpAdd & 0xFU].bInterval = MIDI_HS_BINTERVAL;
    pdev->ep_out[MIDIOutEpAdd & 0xFU].bInterval = MIDI_HS_BINTERVAL;
  }
  else   /* LOW and FULL-speed endpoints */
  {
    pdev->ep_in[MIDIInEpAdd & 0xFU].bInterval = MIDI_FS_BINTERVAL;
    pdev->ep_out[MIDIOutEpAdd & 0xFU].bInterval = MIDI_FS_BINTERVAL;
  }

  /* Open EP IN */
  (void)USBD_LL_OpenEP(pdev, MIDIInEpAdd, USBD_EP_TYPE_BULK,
                       MIDI_EPIN_SIZE);

  pdev->ep_in[MIDIInEpAdd & 0xFU].is_used = 1U;

  /* Open EP OUT */
  (void)USBD_LL_OpenEP(pdev, MIDIOutEpAdd, USBD_EP_TYPE_BULK,
                       MIDI_EPOUT_SIZE);

  pdev->ep_out[MIDIOutEpAdd & 0xFU].is_used = 1U;

  hhid->state = MIDI_IDLE;

  ((USBD_MIDI_ItfTypeDef *)pdev->pUserData[pdev->classId])->Init();

#ifndef USBD_MIDI_OUT_PREPARE_RECEIVE_DISABLED
  /* Prepare Out endpoint to receive 1st packet */
  (void)USBD_LL_PrepareReceive(pdev, MIDIOutEpAdd, hhid->Report_buf,
                               USBD_MIDI_OUTREPORT_BUF_SIZE);
#endif /* USBD_MIDI_OUT_PREPARE_RECEIVE_DISABLED */

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_MIDI_Init
  *         DeInitialize the MIDI layer
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t USBD_MIDI_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

#ifdef USE_USBD_COMPOSITE
  /* Get the Endpoints addresses allocated for this class instance */
  MIDIInEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
  MIDIOutEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

  /* Close MIDI EP IN */
  (void)USBD_LL_CloseEP(pdev, MIDIInEpAdd);
  pdev->ep_in[MIDIInEpAdd & 0xFU].is_used = 0U;
  pdev->ep_in[MIDIInEpAdd & 0xFU].bInterval = 0U;

  /* Close MIDI EP OUT */
  (void)USBD_LL_CloseEP(pdev, MIDIOutEpAdd);
  pdev->ep_out[MIDIOutEpAdd & 0xFU].is_used = 0U;
  pdev->ep_out[MIDIOutEpAdd & 0xFU].bInterval = 0U;

  /* Free allocated memory */
  if (pdev->pClassDataCmsit[pdev->classId] != NULL)
  {
    ((USBD_MIDI_ItfTypeDef *)pdev->pUserData[pdev->classId])->DeInit();
    USBD_free(pdev->pClassDataCmsit[pdev->classId]);
    pdev->pClassDataCmsit[pdev->classId] = NULL;
    pdev->pClassData = NULL;
  }

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_MIDI_Setup
  *         Handle the MIDI specific requests
  * @param  pdev: instance
  * @param  req: usb requests
  * @retval status
  */
static uint8_t USBD_MIDI_Setup(USBD_HandleTypeDef *pdev,
                                     USBD_SetupReqTypedef *req)
{
  USBD_MIDI_HandleTypeDef *hhid = (USBD_MIDI_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  uint16_t len = 0U;
#ifdef USBD_MIDI_CTRL_REQ_GET_REPORT_ENABLED
  uint16_t ReportLength = 0U;
#endif /* USBD_MIDI_CTRL_REQ_GET_REPORT_ENABLED */
  uint8_t  *pbuf = NULL;
  uint16_t status_info = 0U;
  USBD_StatusTypeDef ret = USBD_OK;

  if (hhid == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS:
      switch (req->bRequest)
      {
        case MIDI_REQ_SET_PROTOCOL:
          hhid->Protocol = (uint8_t)(req->wValue);
          break;

        case MIDI_REQ_GET_PROTOCOL:
          (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->Protocol, 1U);
          break;

        case MIDI_REQ_SET_IDLE:
          hhid->IdleState = (uint8_t)(req->wValue >> 8);
          break;

        case MIDI_REQ_GET_IDLE:
          (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->IdleState, 1U);
          break;

        case MIDI_REQ_SET_REPORT:
#ifdef USBD_MIDI_CTRL_REQ_COMPLETE_CALLBACK_ENABLED
          if (((USBD_MIDI_ItfTypeDef *)pdev->pUserData[pdev->classId])->CtrlReqComplete != NULL)
          {
            /* Let the application decide when to enable EP0 to receive the next report */
            ((USBD_MIDI_ItfTypeDef *)pdev->pUserData[pdev->classId])->CtrlReqComplete(req->bRequest,
                                                                                            req->wLength);
          }
#endif /* USBD_MIDI_CTRL_REQ_COMPLETE_CALLBACK_ENABLED */
#ifndef USBD_MIDI_EP0_OUT_PREPARE_RECEIVE_DISABLED
          hhid->IsReportAvailable = 1U;
          (void)USBD_CtlPrepareRx(pdev, hhid->Report_buf,
                                  MIN(req->wLength, USBD_MIDI_OUTREPORT_BUF_SIZE));
#endif /* USBD_MIDI_EP0_OUT_PREPARE_RECEIVE_DISABLED */
          break;
#ifdef USBD_MIDI_CTRL_REQ_GET_REPORT_ENABLED
        case MIDI_REQ_GET_REPORT:
          if (((USBD_MIDI_ItfTypeDef *)pdev->pUserData[pdev->classId])->GetReport != NULL)
          {
            ReportLength = req->wLength;

            /* Get report data buffer */
            pbuf = ((USBD_MIDI_ItfTypeDef *)pdev->pUserData[pdev->classId])->GetReport(&ReportLength);
          }

          if ((pbuf != NULL) && (ReportLength != 0U))
          {
            len = MIN(ReportLength, req->wLength);

            /* Send the report data over EP0 */
            (void)USBD_CtlSendData(pdev, pbuf, len);
          }
          else
          {
#ifdef USBD_MIDI_CTRL_REQ_COMPLETE_CALLBACK_ENABLED
            if (((USBD_MIDI_ItfTypeDef *)pdev->pUserData[pdev->classId])->CtrlReqComplete != NULL)
            {
              /* Let the application decide what to do, keep EP0 data phase in NAK state and
                 use USBD_CtlSendData() when data become available or stall the EP0 data phase */
              ((USBD_MIDI_ItfTypeDef *)pdev->pUserData[pdev->classId])->CtrlReqComplete(req->bRequest,
                                                                                              req->wLength);
            }
            else
            {
              /* Stall EP0 if no data available */
              USBD_CtlError(pdev, req);
            }
#else
            /* Stall EP0 if no data available */
            USBD_CtlError(pdev, req);
#endif /* USBD_MIDI_CTRL_REQ_COMPLETE_CALLBACK_ENABLED */
          }
          break;
#endif /* USBD_MIDI_CTRL_REQ_GET_REPORT_ENABLED */

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;

    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_STATUS:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&status_info, 2U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_GET_DESCRIPTOR:
          /* 音频/MIDI类不应由类驱动返回 HID 类描述符，未知描述符请求直接 STALL */
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;

        case USB_REQ_GET_INTERFACE:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&hhid->AltSetting, 1U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_SET_INTERFACE:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            hhid->AltSetting = (uint8_t)(req->wValue);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_CLEAR_FEATURE:
          break;

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
  }
  return (uint8_t)ret;
}

/**
  * @brief  USBD_MIDI_SendPacket
  *         Send MIDI Report
  * @param  pdev: device instance
  * @param  buff: pointer to report
  * @param  ClassId: The Class ID
  * @retval status
  */
#ifdef USE_USBD_COMPOSITE
uint8_t USBD_MIDI_SendPacket(USBD_HandleTypeDef *pdev,
                                   uint8_t *report, uint16_t len, uint8_t ClassId)
{
  USBD_MIDI_HandleTypeDef *hhid = (USBD_MIDI_HandleTypeDef *)pdev->pClassDataCmsit[ClassId];
#else
uint8_t USBD_MIDI_SendPacket(USBD_HandleTypeDef *pdev,
                                   uint8_t *report, uint16_t len)
{
  USBD_MIDI_HandleTypeDef *hhid = (USBD_MIDI_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
#endif /* USE_USBD_COMPOSITE */

  if (hhid == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

#ifdef USE_USBD_COMPOSITE
  /* Get Endpoint IN address allocated for this class instance */
  MIDIInEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_BULK, ClassId);
#endif /* USE_USBD_COMPOSITE */

  if (pdev->dev_state == USBD_STATE_CONFIGURED)
  {
    if (hhid->state == MIDI_IDLE)
    {
      hhid->state = MIDI_BUSY;
      (void)USBD_LL_Transmit(pdev, MIDIInEpAdd, report, len);
    }
    else
    {
      return (uint8_t)USBD_BUSY;
    }
  }
  return (uint8_t)USBD_OK;
}
#ifndef USE_USBD_COMPOSITE
/**
  * @brief  USBD_MIDI_GetFSCfgDesc
  *         return FS configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_MIDI_GetFSCfgDesc(uint16_t *length)
{
  USBD_EpDescTypeDef *pEpInDesc = USBD_GetEpDesc(USBD_MIDI_CfgDesc, MIDI_EPIN_ADDR);
  USBD_EpDescTypeDef *pEpOutDesc = USBD_GetEpDesc(USBD_MIDI_CfgDesc, MIDI_EPOUT_ADDR);

  if (pEpInDesc != NULL)
  {
    pEpInDesc->wMaxPacketSize = MIDI_EPIN_SIZE;
    pEpInDesc->bInterval = MIDI_FS_BINTERVAL;
  }

  if (pEpOutDesc != NULL)
  {
    pEpOutDesc->wMaxPacketSize = MIDI_EPOUT_SIZE;
    pEpOutDesc->bInterval = MIDI_FS_BINTERVAL;
  }

  *length = (uint16_t)sizeof(USBD_MIDI_CfgDesc);
  return USBD_MIDI_CfgDesc;
}

/**
  * @brief  USBD_MIDI_GetHSCfgDesc
  *         return HS configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_MIDI_GetHSCfgDesc(uint16_t *length)
{
  USBD_EpDescTypeDef *pEpInDesc = USBD_GetEpDesc(USBD_MIDI_CfgDesc, MIDI_EPIN_ADDR);
  USBD_EpDescTypeDef *pEpOutDesc = USBD_GetEpDesc(USBD_MIDI_CfgDesc, MIDI_EPOUT_ADDR);

  if (pEpInDesc != NULL)
  {
    pEpInDesc->wMaxPacketSize = MIDI_EPIN_SIZE;
    pEpInDesc->bInterval = MIDI_HS_BINTERVAL;
  }

  if (pEpOutDesc != NULL)
  {
    pEpOutDesc->wMaxPacketSize = MIDI_EPOUT_SIZE;
    pEpOutDesc->bInterval = MIDI_HS_BINTERVAL;
  }

  *length = (uint16_t)sizeof(USBD_MIDI_CfgDesc);
  return USBD_MIDI_CfgDesc;
}

/**
  * @brief  USBD_MIDI_GetOtherSpeedCfgDesc
  *         return other speed configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_MIDI_GetOtherSpeedCfgDesc(uint16_t *length)
{
  USBD_EpDescTypeDef *pEpInDesc = USBD_GetEpDesc(USBD_MIDI_CfgDesc, MIDI_EPIN_ADDR);
  USBD_EpDescTypeDef *pEpOutDesc = USBD_GetEpDesc(USBD_MIDI_CfgDesc, MIDI_EPOUT_ADDR);

  if (pEpInDesc != NULL)
  {
    pEpInDesc->wMaxPacketSize = MIDI_EPIN_SIZE;
    pEpInDesc->bInterval = MIDI_FS_BINTERVAL;
  }

  if (pEpOutDesc != NULL)
  {
    pEpOutDesc->wMaxPacketSize = MIDI_EPOUT_SIZE;
    pEpOutDesc->bInterval = MIDI_FS_BINTERVAL;
  }

  *length = (uint16_t)sizeof(USBD_MIDI_CfgDesc);
  return USBD_MIDI_CfgDesc;
}
#endif /* USE_USBD_COMPOSITE  */

/**
  * @brief  USBD_MIDI_DataIn
  *         handle data IN Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_MIDI_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  UNUSED(epnum);

  /* Ensure that the FIFO is empty before a new transfer, this condition could
  be caused by  a new transfer before the end of the previous transfer */
  ((USBD_MIDI_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId])->state = MIDI_IDLE;

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_MIDI_DataOut
  *         handle data OUT Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_MIDI_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  UNUSED(epnum);
  USBD_MIDI_HandleTypeDef *hhid;

  if (pdev->pClassDataCmsit[pdev->classId] == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  hhid = (USBD_MIDI_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  uint16_t rx_len = USBD_LL_GetRxDataSize(pdev, epnum);

  /* 将完整数据缓冲和长度传递给应用层 */
  ((USBD_MIDI_ItfTypeDef *)pdev->pUserData[pdev->classId])->OutEvent(hhid->Report_buf, rx_len);

#ifndef USBD_MIDI_OUT_PREPARE_RECEIVE_DISABLED
  /* 处理完成后，立即重装 OUT 端点继续接收后续数据 */
  (void)USBD_LL_PrepareReceive(pdev, MIDIOutEpAdd, hhid->Report_buf,
                               USBD_MIDI_OUTREPORT_BUF_SIZE);
#endif /* USBD_MIDI_OUT_PREPARE_RECEIVE_DISABLED */

  return (uint8_t)USBD_OK;
}


/**
  * @brief  USBD_MIDI_ReceivePacket
  *         prepare OUT Endpoint for reception
  * @param  pdev: device instance
  * @retval status
  */
uint8_t USBD_MIDI_ReceivePacket(USBD_HandleTypeDef *pdev)
{
  USBD_MIDI_HandleTypeDef *hhid;

  if (pdev->pClassDataCmsit[pdev->classId] == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

#ifdef USE_USBD_COMPOSITE
  /* Get OUT Endpoint address allocated for this class instance */
  MIDIOutEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

  hhid = (USBD_MIDI_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  /* Resume USB Out process */
  (void)USBD_LL_PrepareReceive(pdev, MIDIOutEpAdd, hhid->Report_buf,
                               USBD_MIDI_OUTREPORT_BUF_SIZE);

  return (uint8_t)USBD_OK;
}


/**
  * @brief  USBD_MIDI_EP0_RxReady
  *         Handles control request data.
  * @param  pdev: device instance
  * @retval status
  */
static uint8_t USBD_MIDI_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  USBD_MIDI_HandleTypeDef *hhid = (USBD_MIDI_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (hhid == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  if (hhid->IsReportAvailable == 1U)
  {
    ((USBD_MIDI_ItfTypeDef *)pdev->pUserData[pdev->classId])->OutEvent(hhid->Report_buf, 0U);
    hhid->IsReportAvailable = 0U;
  }

  return (uint8_t)USBD_OK;
}

#ifndef USE_USBD_COMPOSITE
/**
  * @brief  DeviceQualifierDescriptor
  *         return Device Qualifier descriptor
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_MIDI_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_MIDI_DeviceQualifierDesc);

  return USBD_MIDI_DeviceQualifierDesc;
}
#endif /* USE_USBD_COMPOSITE  */
/**
  * @brief  USBD_MIDI_RegisterInterface
  * @param  pdev: device instance
  * @param  fops: MIDI Interface callback
  * @retval status
  */
uint8_t USBD_MIDI_RegisterInterface(USBD_HandleTypeDef *pdev,
                                          USBD_MIDI_ItfTypeDef *fops)
{
  if (fops == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  pdev->pUserData[pdev->classId] = fops;

  return (uint8_t)USBD_OK;
}
/**
  * @}
  */


/**
  * @}
  */


/**
  * @}
  */

