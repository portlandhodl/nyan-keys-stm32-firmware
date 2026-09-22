/**
  ******************************************************************************
  * @file    usbd_hid_raw.c
  * @author  Nyan Keys (based on MCD Application Team usbd_hid.c)
  * @brief   Raw HID (vendor defined) class used by the VIA configurator.
  *
  *          This module implements a HID interface with a fixed 32 byte
  *          interrupt IN and interrupt OUT endpoint on the vendor defined
  *          usage page 0xFF60 / usage 0x61. This is the interface the VIA
  *          application (via the WebHID/hidraw node-hid shims) detects and
  *          speaks the VIA protocol on.
  *
  *          OUT report handling is deferred to the application main loop:
  *          DataOut() only marks a pending report, the application fetches
  *          it with USBD_HID_RAW_GetRxReport() and re-arms the endpoint with
  *          USBD_HID_RAW_ReceiveReport() once the report has been processed.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "usbd_hid_raw.h"
#include "usbd_ctlreq.h"

#include <string.h>

#define _HID_RAW_IN_EP 0x82U
#define _HID_RAW_OUT_EP 0x01U
#define _HID_RAW_ITF_NBR 0x01
#define _HID_RAW_STR_DESC_IDX 0x00U

uint8_t HID_RAW_IN_EP = _HID_RAW_IN_EP;
uint8_t HID_RAW_OUT_EP = _HID_RAW_OUT_EP;
uint8_t HID_RAW_ITF_NBR = _HID_RAW_ITF_NBR;
uint8_t HID_RAW_STR_DESC_IDX = _HID_RAW_STR_DESC_IDX;

/** @defgroup USBD_HID_RAW_Private_FunctionPrototypes
  * @{
  */

static uint8_t USBD_HID_RAW_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_HID_RAW_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_HID_RAW_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_HID_RAW_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_HID_RAW_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);

static uint8_t *USBD_HID_RAW_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_HID_RAW_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_HID_RAW_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t *USBD_HID_RAW_GetDeviceQualifierDesc(uint16_t *length);

/**
  * @}
  */

/** @defgroup USBD_HID_RAW_Private_Variables
  * @{
  */

static USBD_HID_RAW_HandleTypeDef USBD_HID_RAW_Instance;

USBD_ClassTypeDef USBD_HID_RAW =
    {
        USBD_HID_RAW_Init,
        USBD_HID_RAW_DeInit,
        USBD_HID_RAW_Setup,
        NULL,                 /* EP0_TxSent */
        NULL,                 /* EP0_RxReady */
        USBD_HID_RAW_DataIn,  /* DataIn */
        USBD_HID_RAW_DataOut, /* DataOut */
        NULL,                 /* SOF */
        NULL,
        NULL,
        USBD_HID_RAW_GetHSCfgDesc,
        USBD_HID_RAW_GetFSCfgDesc,
        USBD_HID_RAW_GetOtherSpeedCfgDesc,
        USBD_HID_RAW_GetDeviceQualifierDesc,
};

/* USB Raw HID device FS Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_HID_RAW_CfgFSDesc[HID_RAW_CONFIG_DESC_SIZE] __ALIGN_END =
    {
        0x09,                        /* bLength: Configuration Descriptor size */
        USB_DESC_TYPE_CONFIGURATION, /* bDescriptorType: Configuration */
        HID_RAW_CONFIG_DESC_SIZE,    /* wTotalLength: Bytes returned */
        0x00,
        0x01, /* bNumInterfaces: 1 interface */
        0x01, /* bConfigurationValue: Configuration value */
        0x00, /* iConfiguration: Index of string descriptor describing the configuration */
#if (USBD_SELF_POWERED == 1U)
        0xE0, /* bmAttributes: Bus Powered according to user configuration */
#else
        0xA0, /* bmAttributes: Bus Powered according to user configuration */
#endif
        USBD_MAX_POWER, /* MaxPower 100 mA: this current is used for detecting Vbus */

        /************** Descriptor of Raw HID interface ****************/
        /* 09 */
        0x09,                    /* bLength: Interface Descriptor size */
        USB_DESC_TYPE_INTERFACE, /* bDescriptorType: Interface descriptor type */
        _HID_RAW_ITF_NBR,        /* bInterfaceNumber: Number of Interface */
        0x00,                    /* bAlternateSetting: Alternate setting */
        0x02,                    /* bNumEndpoints */
        0x03,                    /* bInterfaceClass: HID */
        0x00,                    /* bInterfaceSubClass : 1=BOOT, 0=no boot */
        0x00,                    /* nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse */
        _HID_RAW_STR_DESC_IDX,   /* iInterface: Index of string descriptor */
        /******************** Descriptor of Raw HID HID ********************/
        /* 18 */
        0x09,                     /* bLength: HID Descriptor size */
        HID_RAW_DESCRIPTOR_TYPE,  /* bDescriptorType: HID */
        0x11,                     /* bcdHID: HID Class Spec release number */
        0x01,
        0x00,                     /* bCountryCode: Hardware target country */
        0x01,                     /* bNumDescriptors: Number of HID class descriptors to follow */
        0x22,                     /* bDescriptorType */
        HID_RAW_REPORT_DESC_SIZE, /* wItemLength: Total length of Report descriptor */
        0x00,
        /******************** Descriptor of Raw HID IN endpoint ********************/
        /* 27 */
        0x07,                   /* bLength: Endpoint Descriptor size */
        USB_DESC_TYPE_ENDPOINT, /* bDescriptorType:*/
        _HID_RAW_IN_EP,         /* bEndpointAddress: Endpoint Address (IN) */
        0x03,                   /* bmAttributes: Interrupt endpoint */
        HID_RAW_EPIN_SIZE_FS,   /* wMaxPacketSize: 32 Byte max */
        0x00,
        HID_RAW_FS_BINTERVAL,   /* bInterval: Polling Interval */
        /******************** Descriptor of Raw HID OUT endpoint ********************/
        /* 34 */
        0x07,                   /* bLength: Endpoint Descriptor size */
        USB_DESC_TYPE_ENDPOINT, /* bDescriptorType:*/
        _HID_RAW_OUT_EP,        /* bEndpointAddress: Endpoint Address (OUT) */
        0x03,                   /* bmAttributes: Interrupt endpoint */
        HID_RAW_EPOUT_SIZE_FS,  /* wMaxPacketSize: 32 Byte max */
        0x00,
        HID_RAW_FS_BINTERVAL,   /* bInterval: Polling Interval */
                                /* 41 */
};

/* USB Raw HID device HS Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_HID_RAW_CfgHSDesc[HID_RAW_CONFIG_DESC_SIZE] __ALIGN_END =
    {
        0x09,                        /* bLength: Configuration Descriptor size */
        USB_DESC_TYPE_CONFIGURATION, /* bDescriptorType: Configuration */
        HID_RAW_CONFIG_DESC_SIZE,    /* wTotalLength: Bytes returned */
        0x00,
        0x01, /* bNumInterfaces: 1 interface */
        0x01, /* bConfigurationValue: Configuration value */
        0x00, /* iConfiguration: Index of string descriptor describing the configuration */
#if (USBD_SELF_POWERED == 1U)
        0xE0, /* bmAttributes: Bus Powered according to user configuration */
#else
        0xA0, /* bmAttributes: Bus Powered according to user configuration */
#endif
        USBD_MAX_POWER, /* MaxPower 100 mA: this current is used for detecting Vbus */

        /************** Descriptor of Raw HID interface ****************/
        /* 09 */
        0x09,                    /* bLength: Interface Descriptor size */
        USB_DESC_TYPE_INTERFACE, /* bDescriptorType: Interface descriptor type */
        _HID_RAW_ITF_NBR,        /* bInterfaceNumber: Number of Interface */
        0x00,                    /* bAlternateSetting: Alternate setting */
        0x02,                    /* bNumEndpoints */
        0x03,                    /* bInterfaceClass: HID */
        0x00,                    /* bInterfaceSubClass : 1=BOOT, 0=no boot */
        0x00,                    /* nInterfaceProtocol : 0=none, 1=keyboard, 2=mouse */
        _HID_RAW_STR_DESC_IDX,   /* iInterface: Index of string descriptor */
        /******************** Descriptor of Raw HID HID ********************/
        /* 18 */
        0x09,                     /* bLength: HID Descriptor size */
        HID_RAW_DESCRIPTOR_TYPE,  /* bDescriptorType: HID */
        0x11,                     /* bcdHID: HID Class Spec release number */
        0x01,
        0x00,                     /* bCountryCode: Hardware target country */
        0x01,                     /* bNumDescriptors: Number of HID class descriptors to follow */
        0x22,                     /* bDescriptorType */
        HID_RAW_REPORT_DESC_SIZE, /* wItemLength: Total length of Report descriptor */
        0x00,
        /******************** Descriptor of Raw HID IN endpoint ********************/
        /* 27 */
        0x07,                   /* bLength: Endpoint Descriptor size */
        USB_DESC_TYPE_ENDPOINT, /* bDescriptorType:*/
        _HID_RAW_IN_EP,         /* bEndpointAddress: Endpoint Address (IN) */
        0x03,                   /* bmAttributes: Interrupt endpoint */
        HID_RAW_EPIN_SIZE_HS,   /* wMaxPacketSize: 32 Byte max */
        0x00,
        HID_RAW_HS_BINTERVAL,   /* bInterval: Polling Interval */
        /******************** Descriptor of Raw HID OUT endpoint ********************/
        /* 34 */
        0x07,                   /* bLength: Endpoint Descriptor size */
        USB_DESC_TYPE_ENDPOINT, /* bDescriptorType:*/
        _HID_RAW_OUT_EP,        /* bEndpointAddress: Endpoint Address (OUT) */
        0x03,                   /* bmAttributes: Interrupt endpoint */
        HID_RAW_EPOUT_SIZE_HS,  /* wMaxPacketSize: 32 Byte max */
        0x00,
        HID_RAW_HS_BINTERVAL,   /* bInterval: Polling Interval */
                                /* 41 */
};

/* USB Raw HID device Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_HID_RAW_Desc[HID_RAW_DESC_SIZE] __ALIGN_END =
    {
        /* 18 */
        0x09,                     /* bLength: HID Descriptor size */
        HID_RAW_DESCRIPTOR_TYPE,  /* bDescriptorType: HID */
        0x11,                     /* bcdHID: HID Class Spec release number */
        0x01,
        0x00,                     /* bCountryCode: Hardware target country */
        0x01,                     /* bNumDescriptors: Number of HID class descriptors to follow */
        0x22,                     /* bDescriptorType */
        HID_RAW_REPORT_DESC_SIZE, /* wItemLength: Total length of Report descriptor */
        0x00,
};

/* USB Standard Device Descriptor */
__ALIGN_BEGIN static uint8_t USBD_HID_RAW_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
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

/* Raw HID report descriptor: VIA compatible vendor defined usage page */
__ALIGN_BEGIN static uint8_t HID_RAW_ReportDesc[HID_RAW_REPORT_DESC_SIZE] __ALIGN_END =
    {
        0x06, 0x60, 0xFF, // Usage Page (Vendor Defined 0xFF60)
        0x09, 0x61,       // Usage (0x61)
        0xA1, 0x01,       // Collection (Application)
        0x09, 0x62,       //   Usage (0x62) Raw HID IN data
        0x15, 0x00,       //   Logical Minimum (0)
        0x26, 0xFF, 0x00, //   Logical Maximum (255)
        0x75, 0x08,       //   Report Size (8)
        0x95, 0x20,       //   Report Count (32)
        0x81, 0x02,       //   Input (Data,Var,Abs)
        0x09, 0x63,       //   Usage (0x63) Raw HID OUT data
        0x15, 0x00,       //   Logical Minimum (0)
        0x26, 0xFF, 0x00, //   Logical Maximum (255)
        0x75, 0x08,       //   Report Size (8)
        0x95, 0x20,       //   Report Count (32)
        0x91, 0x02,       //   Output (Data,Var,Abs)
        0xC0              // End Collection
};

/**
  * @}
  */

/** @defgroup USBD_HID_RAW_Private_Functions
  * @{
  */

/**
  * @brief  USBD_HID_RAW_Init
  *         Initialize the Raw HID interface
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t USBD_HID_RAW_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

  USBD_HID_RAW_HandleTypeDef *hraw;

  hraw = &USBD_HID_RAW_Instance;

  if (hraw == NULL)
  {
    pdev->pClassData_HID_RAW = NULL;
    return (uint8_t)USBD_EMEM;
  }

  pdev->pClassData_HID_RAW = (void *)hraw;

  if (pdev->dev_speed == USBD_SPEED_HIGH)
  {
    pdev->ep_in[HID_RAW_IN_EP & 0xFU].bInterval = HID_RAW_HS_BINTERVAL;
    pdev->ep_out[HID_RAW_OUT_EP & 0xFU].bInterval = HID_RAW_HS_BINTERVAL;
    /* Open EP IN + EP OUT - High Speed */
    (void)USBD_LL_OpenEP(pdev, HID_RAW_IN_EP, USBD_EP_TYPE_INTR, HID_RAW_EPIN_SIZE_HS);
    (void)USBD_LL_OpenEP(pdev, HID_RAW_OUT_EP, USBD_EP_TYPE_INTR, HID_RAW_EPOUT_SIZE_HS);
  }
  else /* LOW and FULL-speed endpoints */
  {
    pdev->ep_in[HID_RAW_IN_EP & 0xFU].bInterval = HID_RAW_FS_BINTERVAL;
    pdev->ep_out[HID_RAW_OUT_EP & 0xFU].bInterval = HID_RAW_FS_BINTERVAL;
    /* Open EP IN + EP OUT - Full Speed */
    (void)USBD_LL_OpenEP(pdev, HID_RAW_IN_EP, USBD_EP_TYPE_INTR, HID_RAW_EPIN_SIZE_FS);
    (void)USBD_LL_OpenEP(pdev, HID_RAW_OUT_EP, USBD_EP_TYPE_INTR, HID_RAW_EPOUT_SIZE_FS);
  }

  pdev->ep_in[HID_RAW_IN_EP & 0xFU].is_used = 1U;
  pdev->ep_out[HID_RAW_OUT_EP & 0xFU].is_used = 1U;

  hraw->state = HID_RAW_IDLE;
  hraw->rx_pending = 0U;

  /* Arm the OUT endpoint for the first report */
  (void)USBD_LL_PrepareReceive(pdev, HID_RAW_OUT_EP, hraw->rx_buffer, HID_RAW_REPORT_SIZE);

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_RAW_DeInit
  *         DeInitialize the Raw HID layer
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t USBD_HID_RAW_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

  /* Close Raw HID EPs */
  (void)USBD_LL_CloseEP(pdev, HID_RAW_IN_EP);
  (void)USBD_LL_CloseEP(pdev, HID_RAW_OUT_EP);
  pdev->ep_in[HID_RAW_IN_EP & 0xFU].is_used = 0U;
  pdev->ep_in[HID_RAW_IN_EP & 0xFU].bInterval = 0U;
  pdev->ep_out[HID_RAW_OUT_EP & 0xFU].is_used = 0U;
  pdev->ep_out[HID_RAW_OUT_EP & 0xFU].bInterval = 0U;

  /* Free allocated memory */
  if (pdev->pClassData_HID_RAW != NULL)
  {
    pdev->pClassData_HID_RAW = NULL;
  }

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_RAW_Setup
  *         Handle the Raw HID specific requests
  * @param  pdev: instance
  * @param  req: usb requests
  * @retval status
  */
static uint8_t USBD_HID_RAW_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_HID_RAW_HandleTypeDef *hraw = (USBD_HID_RAW_HandleTypeDef *)pdev->pClassData_HID_RAW;
  USBD_StatusTypeDef ret = USBD_OK;
  uint16_t len;
  uint8_t *pbuf;
  uint16_t status_info = 0U;

  if (hraw == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
  case USB_REQ_TYPE_CLASS:
    switch (req->bRequest)
    {
    case HID_RAW_REQ_SET_PROTOCOL:
      hraw->Protocol = (uint8_t)(req->wValue);
      break;

    case HID_RAW_REQ_GET_PROTOCOL:
      (void)USBD_CtlSendData(pdev, (uint8_t *)&hraw->Protocol, 1U);
      break;

    case HID_RAW_REQ_SET_IDLE:
      hraw->IdleState = (uint8_t)(req->wValue >> 8);
      break;

    case HID_RAW_REQ_GET_IDLE:
      (void)USBD_CtlSendData(pdev, (uint8_t *)&hraw->IdleState, 1U);
      break;

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
      if ((req->wValue >> 8) == HID_RAW_REPORT_DESC)
      {
        len = MIN(HID_RAW_REPORT_DESC_SIZE, req->wLength);
        pbuf = HID_RAW_ReportDesc;
      }
      else if ((req->wValue >> 8) == HID_RAW_DESCRIPTOR_TYPE)
      {
        pbuf = USBD_HID_RAW_Desc;
        len = MIN(HID_RAW_DESC_SIZE, req->wLength);
      }
      else
      {
        USBD_CtlError(pdev, req);
        ret = USBD_FAIL;
        break;
      }
      (void)USBD_CtlSendData(pdev, pbuf, len);
      break;

    case USB_REQ_GET_INTERFACE:
      if (pdev->dev_state == USBD_STATE_CONFIGURED)
      {
        (void)USBD_CtlSendData(pdev, (uint8_t *)&hraw->AltSetting, 1U);
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
        hraw->AltSetting = (uint8_t)(req->wValue);
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
  * @brief  USBD_HID_RAW_GetFSCfgDesc
  *         return FS configuration descriptor
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_RAW_GetFSCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_HID_RAW_CfgFSDesc);

  return USBD_HID_RAW_CfgFSDesc;
}

/**
  * @brief  USBD_HID_RAW_GetHSCfgDesc
  *         return HS configuration descriptor
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_RAW_GetHSCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_HID_RAW_CfgHSDesc);

  return USBD_HID_RAW_CfgHSDesc;
}

/**
  * @brief  USBD_HID_RAW_GetOtherSpeedCfgDesc
  *         return other speed configuration descriptor
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_RAW_GetOtherSpeedCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_HID_RAW_CfgFSDesc);

  return USBD_HID_RAW_CfgFSDesc;
}

/**
  * @brief  USBD_HID_RAW_DataIn
  *         handle data IN Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_HID_RAW_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  UNUSED(epnum);
  /* Ensure that the FIFO is empty before a new transfer, this condition could
  be caused by  a new transfer before the end of the previous transfer */
  ((USBD_HID_RAW_HandleTypeDef *)pdev->pClassData_HID_RAW)->state = HID_RAW_IDLE;

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_RAW_DataOut
  *         handle data OUT Stage - mark a pending report for the application.
  *         The application must fetch it with USBD_HID_RAW_GetRxReport() and
  *         re-arm the endpoint with USBD_HID_RAW_ReceiveReport().
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_HID_RAW_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_HID_RAW_HandleTypeDef *hraw = (USBD_HID_RAW_HandleTypeDef *)pdev->pClassData_HID_RAW;

  if (hraw == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  if (epnum == HID_RAW_OUT_EP)
  {
    hraw->rx_pending = 1U;
  }

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_RAW_RxPending
  *         Check if a 32 byte OUT report is waiting to be consumed
  * @param  pdev: device instance
  * @retval 1 if a report is pending, 0 otherwise
  */
uint8_t USBD_HID_RAW_RxPending(USBD_HandleTypeDef *pdev)
{
  USBD_HID_RAW_HandleTypeDef *hraw = (USBD_HID_RAW_HandleTypeDef *)pdev->pClassData_HID_RAW;

  if (hraw == NULL)
  {
    return 0U;
  }

  return hraw->rx_pending;
}

/**
  * @brief  USBD_HID_RAW_GetRxReport
  *         Copy the pending OUT report and clear the pending flag
  * @param  pdev: device instance
  * @param  report: destination buffer (must hold HID_RAW_REPORT_SIZE bytes)
  * @retval status
  */
uint8_t USBD_HID_RAW_GetRxReport(USBD_HandleTypeDef *pdev, uint8_t *report)
{
  USBD_HID_RAW_HandleTypeDef *hraw = (USBD_HID_RAW_HandleTypeDef *)pdev->pClassData_HID_RAW;

  if (hraw == NULL || hraw->rx_pending == 0U)
  {
    return (uint8_t)USBD_FAIL;
  }

  memcpy(report, (uint8_t *)hraw->rx_buffer, HID_RAW_REPORT_SIZE);
  hraw->rx_pending = 0U;

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_HID_RAW_ReceiveReport
  *         Re-arm the OUT endpoint to receive the next report
  * @param  pdev: device instance
  * @retval status
  */
uint8_t USBD_HID_RAW_ReceiveReport(USBD_HandleTypeDef *pdev)
{
  USBD_HID_RAW_HandleTypeDef *hraw = (USBD_HID_RAW_HandleTypeDef *)pdev->pClassData_HID_RAW;

  if (hraw == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  if (pdev->dev_state == USBD_STATE_CONFIGURED)
  {
    (void)USBD_LL_PrepareReceive(pdev, HID_RAW_OUT_EP, hraw->rx_buffer, HID_RAW_REPORT_SIZE);
  }

  return (uint8_t)USBD_OK;
}

/**
  * @brief  DeviceQualifierDescriptor
  *         return Device Qualifier descriptor
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_HID_RAW_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_HID_RAW_DeviceQualifierDesc);

  return USBD_HID_RAW_DeviceQualifierDesc;
}

/**
  * @brief  USBD_HID_RAW_SendReport
  *         Send Raw HID Report
  * @param  pdev: device instance
  * @param  report: pointer to report
  * @retval status
  */
uint8_t USBD_HID_RAW_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len)
{
  USBD_HID_RAW_HandleTypeDef *hraw = (USBD_HID_RAW_HandleTypeDef *)pdev->pClassData_HID_RAW;

  if (hraw == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  if (pdev->dev_state == USBD_STATE_CONFIGURED)
  {
    if (hraw->state == HID_RAW_IDLE)
    {
      hraw->state = HID_RAW_BUSY;
      if (USBD_LL_Transmit(pdev, HID_RAW_IN_EP, report, len) != (uint8_t)USBD_OK)
      {
        hraw->state = HID_RAW_IDLE; // transmit failed to arm - recover the state machine
      }
    }
  }

  return (uint8_t)USBD_OK;
}

void USBD_Update_HID_RAW_DESC(uint8_t *desc, uint8_t itf_no, uint8_t in_ep, uint8_t out_ep, uint8_t str_idx)
{
  desc[11] = itf_no;
  desc[17] = str_idx;
  desc[29] = in_ep;
  desc[36] = out_ep;

  HID_RAW_IN_EP = in_ep;
  HID_RAW_OUT_EP = out_ep;
  HID_RAW_ITF_NBR = itf_no;
  HID_RAW_STR_DESC_IDX = str_idx;
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

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
