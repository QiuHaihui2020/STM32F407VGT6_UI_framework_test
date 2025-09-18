#include "usbd_composite.h"
#include "usbd_def.h"
#include "usbd_cdc.h"
#include "usbd_msc.h"
#include "usbd_audio.h"

#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_conf.h"
#include <stdio.h>
#include "log_debug.h"

#define USBD_LOG log_debug
// #define USBD_LOG

#define USBD_PRODUCT_XSTR(s) USBD_PRODUCT_STR(s)
#define USBD_PRODUCT_STR(s) #s

#if 1
#define USBD_CMPSIT_VID     1155
#define USBD_CMPSIT_LANGID_STRING     1033
#define USBD_CMPSIT_MANUFACTURER_STRING     "wenyz@wolfGroup"
#define USBD_CMPSIT_PID_FS     22336
#define USBD_CMPSIT_PRODUCT_STRING_FS     "STM32 composite product"
#define USBD_CMPSIT_CONFIGURATION_STRING_FS     "Composite Config"
#define USBD_CMPSIT_INTERFACE_STRING_FS     "Composite Interface"
#else
#define USBD_CMPSIT_VID     1155
#define USBD_CMPSIT_LANGID_STRING     1033
#define USBD_CMPSIT_MANUFACTURER_STRING     "STMicroelectronics"
#define USBD_CMPSIT_PID_FS     22336
#define USBD_CMPSIT_PRODUCT_STRING_FS     "STM32 Virtual ComPort"
#define USBD_CMPSIT_CONFIGURATION_STRING_FS     "CDC Config"
#define USBD_CMPSIT_INTERFACE_STRING_FS     "CDC Interface"
#endif

#define USBD_CDC_INTERFACE_STRING_FS     "CDC Interface"
#define USBD_MSC_INTERFACE_STRING_FS     "MSC Interface"
#define USBD_AUDIO_INTERFACE_STRING_FS     "AUDIO Interface"

#define AUDIO_SAMPLE_FREQ(frq) \
  (uint8_t)(frq), (uint8_t)((frq >> 8)), (uint8_t)((frq >> 16))

#define AUDIO_PACKET_SZE(frq) \
  (uint8_t)(((frq * 2U * 2U) / 1000U) & 0xFFU), (uint8_t)((((frq * 2U * 2U) / 1000U) >> 8) & 0xFFU)

#ifdef USE_USBD_COMPOSITE
#define AUDIO_PACKET_SZE_WORD(frq)     (uint32_t)((((frq) * 2U * 2U)/1000U))
#endif /* USE_USBD_COMPOSITE  */

uint8_t * USBD_CMPSIT_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_CMPSIT_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_CMPSIT_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_CMPSIT_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_CMPSIT_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_CMPSIT_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_CMPSIT_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
#if (USBD_LPM_ENABLED == 1)
uint8_t * USBD_CMPSIT_FS_USR_BOSDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
#endif /* (USBD_LPM_ENABLED == 1) */


USBD_DescriptorsTypeDef usbCmpsitFS_Desc =
{
  USBD_CMPSIT_FS_DeviceDescriptor
, USBD_CMPSIT_FS_LangIDStrDescriptor
, USBD_CMPSIT_FS_ManufacturerStrDescriptor
, USBD_CMPSIT_FS_ProductStrDescriptor
, USBD_CMPSIT_FS_SerialStrDescriptor
, USBD_CMPSIT_FS_ConfigStrDescriptor
, USBD_CMPSIT_FS_InterfaceStrDescriptor
#if (USBD_LPM_ENABLED == 1)
, USBD_CMPSIT_FS_USR_BOSDescriptor
#endif /* (USBD_LPM_ENABLED == 1) */
};

__ALIGN_BEGIN uint8_t USBD_CMPSIT_FS_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END =
{
	0x12,						/*bLength */
	USB_DESC_TYPE_DEVICE, 	  /*bDescriptorType*/
#if (USBD_LPM_ENABLED == 1)
	0x01, 					  /*bcdUSB */ /* changed to USB version 2.01
											 in order to support LPM L1 suspend
											 resume test of USBCV3.0*/
#else
	0x00, 					  /*bcdUSB */
#endif /* (USBD_LPM_ENABLED == 1) */
	0x02,
	// Notify OS that this is a composite device
	0xEF, 					  /*bDeviceClass*/
	0x02, 					  /*bDeviceSubClass*/
	0x01, 					  /*bDeviceProtocol*/
	USB_MAX_EP0_SIZE, 		  /*bMaxPacketSize*/
	LOBYTE(USBD_CMPSIT_VID), 		  /*idVendor*/
	HIBYTE(USBD_CMPSIT_VID), 		  /*idVendor*/
	LOBYTE(USBD_CMPSIT_PID_FS),		  /*idProduct*/
	HIBYTE(USBD_CMPSIT_PID_FS),		  /*idProduct*/
	0x00, 					  /*bcdDevice rel. 2.00*/
	0x02, 					  /* bNumInterfaces */
	USBD_IDX_MFC_STR, 		  /*Index of manufacturer  string*/
	USBD_IDX_PRODUCT_STR, 	  /*Index of product string*/
	USBD_IDX_SERIAL_STR,		  /*Index of serial number string*/
	USBD_MAX_NUM_CONFIGURATION  /*bNumConfigurations*/

};

#if (USBD_LPM_ENABLED == 1)
__ALIGN_BEGIN uint8_t USBD_CMPSIT_FS_BOSDesc[USB_SIZ_BOS_DESC] __ALIGN_END =
{
  0x5,
  USB_DESC_TYPE_BOS,
  0xC,
  0x0,
  0x1,  /* 1 device capability*/
        /* device capability*/
  0x7,
  USB_DEVICE_CAPABITY_TYPE,
  0x2,
  0x2,  /* LPM capability bit set*/
  0x0,
  0x0,
  0x0
};
#endif /* (USBD_LPM_ENABLED == 1) */

/** USB lang identifier descriptor. */
__ALIGN_BEGIN uint8_t USBD_CMPSIT_LangIDDesc[USB_LEN_LANGID_STR_DESC] __ALIGN_END =
{
     USB_LEN_LANGID_STR_DESC,
     USB_DESC_TYPE_STRING,
     LOBYTE(USBD_CMPSIT_LANGID_STRING),
     HIBYTE(USBD_CMPSIT_LANGID_STRING)
};


/* Internal string descriptor. */
__ALIGN_BEGIN uint8_t USBD_CMPSIT_StrDesc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;


__ALIGN_BEGIN uint8_t USBD_CMPSIT_StringSerial[USB_SIZ_STRING_SERIAL] __ALIGN_END = {
  USB_SIZ_STRING_SERIAL,
  USB_DESC_TYPE_STRING,
};
 

uint8_t * USBD_CMPSIT_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
	USBD_LOG("USBD_CMPSIT_FS_DeviceDescriptor, speed %d, length %d\n", speed, *length);
  UNUSED(speed);
  *length = sizeof(USBD_CMPSIT_FS_DeviceDesc);
  return USBD_CMPSIT_FS_DeviceDesc;
}

uint8_t * USBD_CMPSIT_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
	USBD_LOG("USBD_CMPSIT_FS_LangIDStrDescriptor, speed %d, length %d\n", speed, *length);
  UNUSED(speed);
  *length = sizeof(USBD_CMPSIT_LangIDDesc);
  return USBD_CMPSIT_LangIDDesc;
}
uint8_t * USBD_CMPSIT_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
	USBD_LOG("USBD_CMPSIT_FS_ManufacturerStrDescriptor, speed %d, length %d\n", speed, *length);
  UNUSED(speed);
  USBD_GetString((uint8_t *)USBD_CMPSIT_MANUFACTURER_STRING, USBD_CMPSIT_StrDesc, length);
  return USBD_CMPSIT_StrDesc;
}

uint8_t * USBD_CMPSIT_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
	USBD_LOG("USBD_CMPSIT_FS_ProductStrDescriptor, speed %d, length %d\n", speed, *length);
  if(speed == 0)
  {
    USBD_GetString((uint8_t *)USBD_CMPSIT_PRODUCT_STRING_FS, USBD_CMPSIT_StrDesc, length);
  }
  else
  {
    USBD_GetString((uint8_t *)USBD_CMPSIT_PRODUCT_STRING_FS, USBD_CMPSIT_StrDesc, length);
  }
  return USBD_CMPSIT_StrDesc;
}

static void IntToUnicode(uint32_t value, uint8_t * pbuf, uint8_t len)
{
  uint8_t idx = 0;

  for (idx = 0; idx < len; idx++)
  {
    if (((value >> 28)) < 0xA)
    {
      pbuf[2 * idx] = (value >> 28) + '0';
    }
    else
    {
      pbuf[2 * idx] = (value >> 28) + 'A' - 10;
    }

    value = value << 4;

    pbuf[2 * idx + 1] = 0;
  }
}
static void Get_CMPSIT_SerialNum(void)
{
	USBD_LOG("Get_CMPSIT_SerialNum\n");
  uint32_t deviceserial0, deviceserial1, deviceserial2;

  deviceserial0 = *(uint32_t *) DEVICE_ID1;
  deviceserial1 = *(uint32_t *) DEVICE_ID2;
  deviceserial2 = *(uint32_t *) DEVICE_ID3;

  deviceserial0 += deviceserial2;

  if (deviceserial0 != 0)
  {
    IntToUnicode(deviceserial0, &USBD_CMPSIT_StringSerial[2], 8);
    IntToUnicode(deviceserial1, &USBD_CMPSIT_StringSerial[18], 4);
  }
}
uint8_t * USBD_CMPSIT_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
	USBD_LOG("USBD_CMPSIT_FS_SerialStrDescriptor, speed %d, length %d\n", speed, *length);
  UNUSED(speed);
  *length = USB_SIZ_STRING_SERIAL;

  /* Update the serial number string descriptor with the data from the unique
   * ID */
  Get_CMPSIT_SerialNum();
  /* USER CODE BEGIN USBD_FS_SerialStrDescriptor */

  /* USER CODE END USBD_FS_SerialStrDescriptor */
  return (uint8_t *) USBD_CMPSIT_StringSerial;
}

uint8_t * USBD_CMPSIT_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
	USBD_LOG("USBD_CMPSIT_FS_InterfaceStrDescriptor, speed %d, length %d\n", speed, *length);
  if(speed == 0)
  {
    USBD_GetString((uint8_t *)USBD_CMPSIT_INTERFACE_STRING_FS, USBD_CMPSIT_StrDesc, length);
  }
  else
  {
    USBD_GetString((uint8_t *)USBD_CMPSIT_INTERFACE_STRING_FS, USBD_CMPSIT_StrDesc, length);
  }
  return USBD_CMPSIT_StrDesc;
}


uint8_t * USBD_CMPSIT_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
	USBD_LOG("USBD_CMPSIT_FS_ConfigStrDescriptor, speed %d, length %d\n", speed, *length);
  if(speed == USBD_SPEED_HIGH)
  {
    USBD_GetString((uint8_t *)USBD_CMPSIT_CONFIGURATION_STRING_FS, USBD_CMPSIT_StrDesc, length);
  }
  else
  {
    USBD_GetString((uint8_t *)USBD_CMPSIT_CONFIGURATION_STRING_FS, USBD_CMPSIT_StrDesc, length);
  }
  return USBD_CMPSIT_StrDesc;
}


#if (USBD_LPM_ENABLED == 1)
/**
  * @brief  Return the BOS descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_CMPSIT_FS_USR_BOSDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = sizeof(USBD_CMPSIT_FS_BOSDesc);
  return (uint8_t*)USBD_CMPSIT_FS_BOSDesc;
}
#endif /* (USBD_LPM_ENABLED == 1) */


#define USB_CMPSIT_CONFIG_DESC_SIZ                   (9 \
                                                     + (USB_CDC_CONFIG_DESC_SIZ-9+8) \
                                                     +(USB_MSC_CONFIG_DESC_SIZ-9 +8) \
                                                     +(USB_AUDIO_CONFIG_DESC_SIZ-9 +8) \
                                                    )// (67U+39U)

__ALIGN_BEGIN static uint8_t USBD_CMPSIT_CfgDesc[USB_CMPSIT_CONFIG_DESC_SIZ] __ALIGN_END =
{
  /* Configuration Descriptor */
  0x09,                         /* bLength: Configuration Descriptor size */
  USB_DESC_TYPE_CONFIGURATION,  /* bDescriptorType: Configuration */
  LOBYTE(USB_CMPSIT_CONFIG_DESC_SIZ), /* wTotalLength */
  HIBYTE(USB_CMPSIT_CONFIG_DESC_SIZ),
  0x05,                         /* bNumInterfaces: CDC(2)+MSC(1)+Audio(2)=5 interfaces */
  0x01,                         /* bConfigurationValue */
  0x00,                         /* iConfiguration */
#if (USBD_SELF_POWERED == 1U)
  0xC0,                         /* bmAttributes: Self Powered */
#else
  0x80,                         /* bmAttributes: Bus Powered */
#endif
  USBD_MAX_POWER,               /* MaxPower */

  /* CDC IAD */
  0x08,
  0x0B,
  0x00,         /* bFirstInterface */
  0x02,         /* bInterfaceCount */
  0x02,         /* bFunctionClass: Communication Interface Class */
  0x02,         /* bFunctionSubClass: Abstract Control Model */
  0x01,         /* bFunctionProtocol: AT commands */
  0x06,         /* iFunction */

  /* CDC Communication Interface Descriptor */
  0x09,
  USB_DESC_TYPE_INTERFACE,
  0x00,         /* bInterfaceNumber */
  0x00,
  0x01,         /* One endpoint: CDC_CMD_EP */
  0x02,
  0x02,
  0x01,
  0x06,

  /* CDC Header Functional Descriptor */
  0x05,
  0x24,
  0x00,
  0x10, 0x01,

  /* CDC Call Management Functional Descriptor */
  0x05,
  0x24,
  0x01,
  0x00,
  0x01,         /* bDataInterface */

  /* CDC ACM Functional Descriptor */
  0x04,
  0x24,
  0x02,
  0x02,

  /* CDC Union Functional Descriptor */
  0x05,
  0x24,
  0x06,
  0x00,         /* Master interface */
  0x01,         /* Slave interface */

  /* CDC Command Endpoint Descriptor */
  0x07,
  USB_DESC_TYPE_ENDPOINT,
  CDC_CMD_EP,
  0x03,
  LOBYTE(CDC_CMD_PACKET_SIZE),
  HIBYTE(CDC_CMD_PACKET_SIZE),
  CDC_FS_BINTERVAL,

  /* CDC Data Interface Descriptor */
  0x09,
  USB_DESC_TYPE_INTERFACE,
  0x01,         /* bInterfaceNumber */
  0x00,
  0x02,         /* Two endpoints: IN/OUT */
  0x0A,
  0x00,
  0x00,
  0x06,

  /* CDC Data OUT Endpoint Descriptor */
  0x07,
  USB_DESC_TYPE_ENDPOINT,
  CDC_OUT_EP,
  0x02,
  LOBYTE(CDC_DATA_FS_MAX_PACKET_SIZE),
  HIBYTE(CDC_DATA_FS_MAX_PACKET_SIZE),
  0x00,

  /* CDC Data IN Endpoint Descriptor */
  0x07,
  USB_DESC_TYPE_ENDPOINT,
  CDC_IN_EP,
  0x02,
  LOBYTE(CDC_DATA_FS_MAX_PACKET_SIZE),
  HIBYTE(CDC_DATA_FS_MAX_PACKET_SIZE),
  0x00,

  /* MSC IAD */
  0x08,
  0x0B,
  0x02,        /* bFirstInterface: MSC interface */
  0x01,
  0x08,
  0x06,
  0x50,
  0x07,

  /* MSC Interface Descriptor */
  0x09,
  USB_DESC_TYPE_INTERFACE,
  0x02,        /* MSC interface number */
  0x00,
  0x02,        /* Two endpoints */
  0x08,
  0x06,
  0x50,
  0x07,

  /* MSC IN Endpoint */
  0x07,
  USB_DESC_TYPE_ENDPOINT,
  MSC_EPIN_ADDR,
  0x02,
  LOBYTE(MSC_MAX_FS_PACKET),
  HIBYTE(MSC_MAX_FS_PACKET),
  0x00,

  /* MSC OUT Endpoint */
  0x07,
  USB_DESC_TYPE_ENDPOINT,
  MSC_EPOUT_ADDR,
  0x02,
  LOBYTE(MSC_MAX_FS_PACKET),
  HIBYTE(MSC_MAX_FS_PACKET),
  0x00,

  /* Audio IAD */
  0x08,
  0x0B,
  0x03,        /* bFirstInterface: Audio Control */
  0x02,
  0x01,
  0x01,
  0x00,
  0x00,

  /* Audio Control Interface Descriptor */
  AUDIO_INTERFACE_DESC_SIZE,
  USB_DESC_TYPE_INTERFACE,
  0x03,        /* AC interface number */
  0x00,
  0x00,
  USB_DEVICE_CLASS_AUDIO,
  AUDIO_SUBCLASS_AUDIOCONTROL,
  AUDIO_PROTOCOL_UNDEFINED,
  0x00,

  /* AC Class-specific Header Descriptor */
  AUDIO_INTERFACE_DESC_SIZE,
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,
  AUDIO_CONTROL_HEADER,
  0x00, 0x01,      /* bcdADC 1.0 */
  0x27, 0x00,      /* wTotalLength */
  0x01,            /* bInCollection */
  0x04,            /* baInterfaceNr: AS interface */

  /* Input Terminal Descriptor */
  AUDIO_INPUT_TERMINAL_DESC_SIZE,
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,
  AUDIO_CONTROL_INPUT_TERMINAL,
  0x01,
  0x01, 0x01,      /* USB streaming */
  0x00,
  0x02,            /* stereo */
  0x03, 0x00,      /* wChannelConfig */
  0x00,
  0x00,

  /* Feature Unit Descriptor */
  0x09,
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,
  AUDIO_CONTROL_FEATURE_UNIT,
  0x02,
  0x01,
  0x01,
  AUDIO_CONTROL_MUTE,
  0x00,
  0x00,

  /* Output Terminal Descriptor */
  0x09,
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,
  AUDIO_CONTROL_OUTPUT_TERMINAL,
  0x03,
  0x01, 0x03,
  0x00,
  0x02,  /* bSourceID: Feature Unit */
  0x00,

  /* Audio Streaming Zero Bandwidth Interface Descriptor */
  AUDIO_INTERFACE_DESC_SIZE,
  USB_DESC_TYPE_INTERFACE,
  0x04,        /* AS interface number */
  0x00,
  0x00,
  USB_DEVICE_CLASS_AUDIO,
  AUDIO_SUBCLASS_AUDIOSTREAMING,
  AUDIO_PROTOCOL_UNDEFINED,
  0x00,

  /* Audio Streaming Operational Interface Descriptor */
  AUDIO_INTERFACE_DESC_SIZE,
  USB_DESC_TYPE_INTERFACE,
  0x04,
  0x01,
  0x01,        /* one endpoint: AUDIO_OUT_EP */
  USB_DEVICE_CLASS_AUDIO,
  AUDIO_SUBCLASS_AUDIOSTREAMING,
  AUDIO_PROTOCOL_UNDEFINED,
  0x00,

  /* AS General Descriptor */
  AUDIO_STREAMING_INTERFACE_DESC_SIZE,
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,
  AUDIO_STREAMING_GENERAL,
  0x01,  /* TerminalLink */
  0x01,  /* Delay */
  0x01, 0x00,  /* PCM */

  /* Format Type Descriptor */
  0x0B,
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,
  AUDIO_STREAMING_FORMAT_TYPE,
  AUDIO_FORMAT_TYPE_I,
  0x02,        /* stereo */
  0x02,        /* subframe size 2 bytes */
  16,          /* bit resolution */
  0x01,        /* one sample freq */
  AUDIO_SAMPLE_FREQ(USBD_AUDIO_FREQ),

  /* Audio Streaming Endpoint Descriptor */
  AUDIO_STANDARD_ENDPOINT_DESC_SIZE,
  USB_DESC_TYPE_ENDPOINT,
  AUDIO_OUT_EP, /* OUT endpoint */
  USBD_EP_TYPE_ISOC,
  AUDIO_PACKET_SZE(USBD_AUDIO_FREQ),
  AUDIO_FS_BINTERVAL,
  0x00,
  0x00,

  /* Audio Streaming Class-specific Endpoint Descriptor */
  AUDIO_STREAMING_ENDPOINT_DESC_SIZE,
  AUDIO_ENDPOINT_DESCRIPTOR_TYPE,
  AUDIO_ENDPOINT_GENERAL,
  0x00,
  0x00,
  0x00, 0x00
};


__ALIGN_BEGIN static uint8_t USBD_CMPSIT_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
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

static uint8_t *USBD_CMPSIT_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_CMPSIT_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_CMPSIT_GetOtherSpeedCfgDesc(uint16_t *length);
uint8_t *USBD_CMPSIT_GetDeviceQualifierDescriptor(uint16_t *length);


/* This structure is used only for the Configuration descriptors and Device Qualifier */
USBD_ClassTypeDef  USBD_CMPSIT =
{
  NULL, /* Init, */
  NULL, /* DeInit, */
  NULL, /* Setup, */
  NULL, /* EP0_TxSent, */
  NULL, /* EP0_RxReady, */
  NULL, /* DataIn, */
  NULL, /* DataOut, */
  NULL, /* SOF,  */
  NULL,
  NULL,
#ifdef USE_USB_HS
  USBD_CMPSIT_GetHSCfgDesc,
#else
  NULL,
#endif /* USE_USB_HS */
  USBD_CMPSIT_GetFSCfgDesc,
  USBD_CMPSIT_GetOtherSpeedCfgDesc,
  USBD_CMPSIT_GetDeviceQualifierDescriptor,
#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
  NULL,
#endif /* USBD_SUPPORT_USER_STRING_DESC */
};


#ifdef USE_USBD_COMPOSITE
void USBD_CMPSIT_AddClass(USBD_HandleTypeDef *pdev, USBD_ClassTypeDef *pclass, USBD_CompositeClassTypeDef classtype, uint8_t *EpAddr)
{
	USBD_LOG("USBD_CMPSIT_AddClass, classId %d, type %d\r\n",pdev->classId,classtype);
	switch(classtype) {
		case CLASS_TYPE_CDC:{
			pdev->tclasslist[pdev->classId].ClassType = CLASS_TYPE_CDC;
			
			pdev->tclasslist[pdev->classId].Active = 1U;
			pdev->tclasslist[pdev->classId].NumEps = 3;
			
			pdev->tclasslist[pdev->classId].Eps[0].add = CDC_CMD_EP;
			pdev->tclasslist[pdev->classId].Eps[0].type = USBD_EP_TYPE_INTR;
			pdev->tclasslist[pdev->classId].Eps[0].size = CDC_CMD_PACKET_SIZE;
			pdev->tclasslist[pdev->classId].Eps[0].is_used = 1U;
			
			pdev->tclasslist[pdev->classId].Eps[1].add = CDC_OUT_EP;
			pdev->tclasslist[pdev->classId].Eps[1].type = USBD_EP_TYPE_BULK;
			pdev->tclasslist[pdev->classId].Eps[1].size = CDC_DATA_FS_MAX_PACKET_SIZE;
			pdev->tclasslist[pdev->classId].Eps[1].is_used = 1U;
			
			pdev->tclasslist[pdev->classId].Eps[2].add = CDC_IN_EP;
			pdev->tclasslist[pdev->classId].Eps[2].type = USBD_EP_TYPE_BULK;
			pdev->tclasslist[pdev->classId].Eps[2].size = CDC_DATA_FS_MAX_PACKET_SIZE;
			pdev->tclasslist[pdev->classId].Eps[2].is_used = 1U;

			pdev->tclasslist[pdev->classId].NumIf = 2;
			pdev->tclasslist[pdev->classId].Ifs[0] = 0;
			pdev->tclasslist[pdev->classId].Ifs[1] = 1;	
			
		}break;

		case CLASS_TYPE_MSC:{
			pdev->tclasslist[pdev->classId].ClassType = CLASS_TYPE_MSC;
			
			pdev->tclasslist[pdev->classId].Active = 1U;
			pdev->tclasslist[pdev->classId].NumEps = 2;
			
			pdev->tclasslist[pdev->classId].Eps[0].add = MSC_EPIN_ADDR;
			pdev->tclasslist[pdev->classId].Eps[0].type = USBD_EP_TYPE_BULK;
			pdev->tclasslist[pdev->classId].Eps[0].size = MSC_MAX_FS_PACKET;
			pdev->tclasslist[pdev->classId].Eps[0].is_used = 1U;
			
			pdev->tclasslist[pdev->classId].Eps[1].add = MSC_EPOUT_ADDR;
			pdev->tclasslist[pdev->classId].Eps[1].type = USBD_EP_TYPE_BULK;
			pdev->tclasslist[pdev->classId].Eps[1].size = MSC_MAX_FS_PACKET;
			pdev->tclasslist[pdev->classId].Eps[1].is_used = 1U;
			

			pdev->tclasslist[pdev->classId].NumIf = 1;
			pdev->tclasslist[pdev->classId].Ifs[0] = 2;						
			
		}break;

		case CLASS_TYPE_AUDIO:{
			pdev->tclasslist[pdev->classId].ClassType = USBD_EP_TYPE_ISOC;
			
			pdev->tclasslist[pdev->classId].Active = 1U;
			pdev->tclasslist[pdev->classId].NumEps = 1;
			
			pdev->tclasslist[pdev->classId].Eps[0].add = AUDIO_OUT_EP;
			pdev->tclasslist[pdev->classId].Eps[0].type = USBD_EP_TYPE_ISOC;
			pdev->tclasslist[pdev->classId].Eps[0].size = AUDIO_PACKET_SZE(USBD_AUDIO_FREQ);;
			pdev->tclasslist[pdev->classId].Eps[0].is_used = 1U;
			

			pdev->tclasslist[pdev->classId].NumIf = 1;
			pdev->tclasslist[pdev->classId].Ifs[0] = 3;
      pdev->tclasslist[pdev->classId].Ifs[1] = 4;  			
			
		}break;
		default:break;
	}
	pdev->tclasslist[pdev->classId].CurrPcktSze = 0U;
		
}

#endif

uint8_t * USBD_UsrStrDescriptor(struct _USBD_HandleTypeDef *pdev, uint8_t index,  uint16_t *length)
{
	USBD_LOG("USBD_UsrStrDescriptor, index %d, length %d\r\n", index, *length);
  *length = 0;
  //printf("index=%d\r\n",index);
 /*  if (USBD_IDX_MICROSOFT_DESC_STR == index) {
    *length = sizeof (USBD_MS_OS_StringDescriptor);
    return USBD_MS_OS_StringDescriptor;
  } else if (USBD_IDX_ODRIVE_INTF_STR == index) {
    USBD_GetString((uint8_t *)USBD_PRODUCT_XSTR(NATIVE_STRING), USBD_StrDesc, length);
    return USBD_StrDesc;
  }  */
  if (USBD_IDX_CDC_INTF_STR == index) {
    USBD_GetString((uint8_t *)USBD_CDC_INTERFACE_STRING_FS, USBD_CMPSIT_StrDesc, length);
    return USBD_CMPSIT_StrDesc;
  }  
  else if (USBD_IDX_MSC_INTF_STR == index) {    
    USBD_GetString((uint8_t *)USBD_MSC_INTERFACE_STRING_FS, USBD_CMPSIT_StrDesc, length);
    return USBD_CMPSIT_StrDesc;
  }  else if (USBD_IDX_AUDIO_INTF_STR == index) {    
    USBD_GetString((uint8_t *)USBD_AUDIO_INTERFACE_STRING_FS, USBD_CMPSIT_StrDesc, length);
    return USBD_CMPSIT_StrDesc;
  }  

  return NULL;
}

static uint8_t *USBD_CMPSIT_GetFSCfgDesc(uint16_t *length)
{
	*length = (uint16_t)sizeof(USBD_CMPSIT_CfgDesc);
	USBD_LOG("USBD_CMPSIT_GetFSCfgDesc length %d\r\n", *length);
	return USBD_CMPSIT_CfgDesc;
}
static uint8_t *USBD_CMPSIT_GetHSCfgDesc(uint16_t *length)
{
	USBD_LOG("USBD_CMPSIT_GetHSCfgDesc length %d\r\n", *length);
	return NULL;
}
static uint8_t *USBD_CMPSIT_GetOtherSpeedCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_CMPSIT_CfgDesc);
  USBD_LOG("USBD_CMPSIT_GetOtherSpeedCfgDesc length %d\r\n", *length);
  return USBD_CMPSIT_CfgDesc;
}
uint8_t *USBD_CMPSIT_GetDeviceQualifierDescriptor(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_CMPSIT_DeviceQualifierDesc);
  USBD_LOG("USBD_CMPSIT_GetDeviceQualifierDescriptor length %d\r\n", *length);
  return USBD_CMPSIT_DeviceQualifierDesc;
}

uint8_t USBD_get_composite_class_id(USBD_HandleTypeDef *pdev, uint8_t classType)
{
  for (uint8_t i = 0; i < USBD_MAX_SUPPORTED_CLASS; i++)
  {
    if (pdev->tclasslist[i].ClassType == classType)
    {
      return i;
    }
  }
  return 0xFF;
}