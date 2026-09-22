/**
  ******************************************************************************
  * @file    usbd_hid_raw.h
  * @author  Nyan Keys (based on MCD Application Team usbd_hid.c)
  * @brief   Header file for the usbd_hid_raw.c file.
  *
  * @description
  * Vendor specific "Raw HID" interface used by the VIA configurator.
  * Presents a 32 byte IN and 32 byte OUT interrupt endpoint pair on
  * usage page 0xFF60 / usage 0x61 which is the interface VIA scans for.
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USB_HID_RAW_H
#define __USB_HID_RAW_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include  "usbd_ioreq.h"

/** @defgroup USBD_HID_RAW_Exported_Defines
  * @{
  */

#define HID_RAW_STR_DESC                                    "Nyan Keys - VIA Raw HID"

#define HID_RAW_REPORT_SIZE                                 0x20U // 32 Byte fixed VIA reports

#define HID_RAW_EPIN_SIZE_HS                                HID_RAW_REPORT_SIZE
#define HID_RAW_EPIN_SIZE_FS                                HID_RAW_REPORT_SIZE
#define HID_RAW_EPOUT_SIZE_HS                               HID_RAW_REPORT_SIZE
#define HID_RAW_EPOUT_SIZE_FS                               HID_RAW_REPORT_SIZE

#define HID_RAW_CONFIG_DESC_SIZE                            41U // 9 cfg + 9 itf + 9 hid + 7 epin + 7 epout
#define HID_RAW_DESC_SIZE                                   9U

#define HID_RAW_REPORT_DESC_SIZE                            35U

#define HID_RAW_DESCRIPTOR_TYPE                             0x21U
#define HID_RAW_REPORT_DESC                                 0x22U

#ifndef HID_RAW_HS_BINTERVAL
#define HID_RAW_HS_BINTERVAL                                0x01U
#endif /* HID_RAW_HS_BINTERVAL */

#ifndef HID_RAW_FS_BINTERVAL
#define HID_RAW_FS_BINTERVAL                                0x01U
#endif /* HID_RAW_FS_BINTERVAL */

#define HID_RAW_REQ_SET_PROTOCOL                            0x0BU
#define HID_RAW_REQ_GET_PROTOCOL                            0x03U

#define HID_RAW_REQ_SET_IDLE                                0x0AU
#define HID_RAW_REQ_GET_IDLE                                0x02U

#define HID_RAW_REQ_SET_REPORT                              0x09U
#define HID_RAW_REQ_GET_REPORT                              0x01U
/**
  * @}
  */

/** @defgroup USBD_HID_RAW_Exported_TypesDefinitions
  * @{
  */
typedef enum
{
  HID_RAW_IDLE = 0,
  HID_RAW_BUSY,
} HID_RAW_StateTypeDef;

typedef struct
{
  uint32_t Protocol;
  uint32_t IdleState;
  uint32_t AltSetting;
  uint32_t state;                  /* IN endpoint state (HID_RAW_IDLE/HID_RAW_BUSY) - uint32: this toolchain packs small enums, and DMA buffers must stay 32-bit aligned */
  volatile uint8_t rx_pending;     /* A 32 byte OUT report is waiting for the application */
  __ALIGN_BEGIN uint8_t rx_buffer[HID_RAW_REPORT_SIZE] __ALIGN_END; /* OUT report staging buffer - DMA target, must be 32-bit aligned */
} USBD_HID_RAW_HandleTypeDef;
/**
  * @}
  */

/** @defgroup USBD_HID_RAW_Exported_Variables
  * @{
  */

extern USBD_ClassTypeDef USBD_HID_RAW;

extern uint8_t HID_RAW_IN_EP;
extern uint8_t HID_RAW_OUT_EP;
extern uint8_t HID_RAW_ITF_NBR;
extern uint8_t HID_RAW_STR_DESC_IDX;

/**
  * @}
  */

/** @defgroup USBD_HID_RAW_Exported_Functions
  * @{
  */
uint8_t USBD_HID_RAW_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len);
uint8_t USBD_HID_RAW_ReceiveReport(USBD_HandleTypeDef *pdev);
uint8_t USBD_HID_RAW_RxPending(USBD_HandleTypeDef *pdev);
uint8_t USBD_HID_RAW_GetRxReport(USBD_HandleTypeDef *pdev, uint8_t *report);

void USBD_Update_HID_RAW_DESC(uint8_t *desc, uint8_t itf_no, uint8_t in_ep, uint8_t out_ep, uint8_t str_idx);

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* __USB_HID_RAW_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
