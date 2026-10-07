/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdbool.h"
#include "string.h"
#include "nyan_eeprom_map.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SPI4_SS_Pin GPIO_PIN_3
#define SPI4_SS_GPIO_Port GPIOE
#define FPGA_config_nrst_Pin GPIO_PIN_4
#define FPGA_config_nrst_GPIO_Port GPIOE
#define Nyan_FPGA_Config_Done_Pin GPIO_PIN_13
#define Nyan_FPGA_Config_Done_GPIO_Port GPIOC
#define keys_fpga_resetn_Pin GPIO_PIN_0
#define keys_fpga_resetn_GPIO_Port GPIOC
#define keys_ack_Pin GPIO_PIN_2
#define keys_ack_GPIO_Port GPIOC
#define Nyan_Keys_LED0_Pin GPIO_PIN_0
#define Nyan_Keys_LED0_GPIO_Port GPIOD
#define Nyan_Keys_LED1_Pin GPIO_PIN_1
#define Nyan_Keys_LED1_GPIO_Port GPIOD
#define Nyan_Keys_LED2_Pin GPIO_PIN_2
#define Nyan_Keys_LED2_GPIO_Port GPIOD
#define Nyan_Keys_LED3_Pin GPIO_PIN_3
#define Nyan_Keys_LED3_GPIO_Port GPIOD
#define Nyan_Keys_LED4_Pin GPIO_PIN_4
#define Nyan_Keys_LED4_GPIO_Port GPIOD
#define Nyan_DFU_Enable_Pin GPIO_PIN_0
#define Nyan_DFU_Enable_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */
#define NYAN_SPI_GPIO_0_Pin GPIO_PIN_3
#define NYAN_SPI_GPIO_0_Port GPIOE
#define NYAN_SPI_GPIO_1_Pin GPIO_PIN_5
#define NYAN_SPI_GPIO_1_Port GPIOE
#define NYAN_SPI_GPIO_2_Pin GPIO_PIN_6
#define NYAN_SPI_GPIO_2_Port GPIOE

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
