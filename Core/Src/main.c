/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "i2c.h"
#include "rng.h"
#include "spi.h"
#include "tim.h"
#include "usb_otg.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
// USB Composite device support
#include "usb_device.h"
#include "usbd_cdc_acm_if.h"
#include "usbd_hid_keyboard.h"
// Nyan Keys Specific Hardware
#include "24xx_eeprom.h"
#include "iceuncompr.h"
#include "lattice_ice_hx.h"
// NyanOS and Packages
#include "nyan_os.h"
#include "nyan_health.h"
#include "nyan_leds.h"
#include "nyan_strings.h"
#include "nyan_bitcoin.h"
#include "nyan_keys.h"
#include "nyan_via.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
extern USBD_HandleTypeDef hUsbDevice;

// Volatile Interrupt Variables
volatile NyanOS nos;                                  // NyanOS - Main Operating System
volatile double system_status_led_angle;              // Used in the Sin^2(x) + Cos^2(x) = 1 [LED PWM]
volatile NyanKeys nyan_keys;                          // Nyan Keys FPGA Switch driver FPGA -> SPI -> STM32
volatile NyanKeyBoardDescriptor nyan_hid_report;      // Global HID Report used in the nyan keys
volatile NyanKeyBoardDescriptor nyan_hid_report_prv;  // Global HID Report used for comparison optimization

// Non-Volatile Globals
Eeprom24xx   nos_eeprom;   // 24xx Based EEPROM
Iceuncompr   ice_uncompr;  // Decompression agent - FPGA Bitstream 
LatticeIceHX nos_fpga;     // Lattice ICE40HX4k FPGA driver
NyanBitcoin  nyan_bitcoin; // Nyan Keys Background Bitcoin Miner
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  EepromInit(&nos_eeprom, true, true);
  
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI2_Init();
  MX_SPI4_Init();
  MX_I2C1_Init();
  MX_TIM7_Init();
  MX_TIM6_Init();
  MX_TIM1_Init();
  MX_RNG_Init();
  MX_TIM8_Init();
  MX_TIM14_Init();
  MX_USB_OTG_HS_PCD_Init();
  /* USER CODE BEGIN 2 */
  NyanHealthInit();                    // Watchdog + reset cause - everything below must feed it within NYAN_WATCHDOG_TIMEOUT_MS
  // Activate the STM32F7 timer interrupts
  HAL_TIM_Base_Start_IT(&htim1);
  HAL_TIM_Base_Start_IT(&htim7);
  HAL_TIM_Base_Start_IT(&htim6);
  HAL_TIM_Base_Start_IT(&htim14);
  HAL_TIM_OC_Start_IT(&htim1, TIM_CHANNEL_1);
  HAL_TIM_OC_Start_IT(&htim1, TIM_CHANNEL_2);
  HAL_TIM_OC_Start_IT(&htim8, TIM_CHANNEL_1);
  // USB composite device creation
  MX_USB_DEVICE_Init();
  NyanOsInit(&nos);                    // NyanOS (NOS) Initialization
  NyanViaInit();                       // VIA dynamic keymap - load from EEPROM or program factory defaults (before FPGAInit: VIA works even if the FPGA never configures)
  FPGAInit((LatticeIceHX*)&nos_fpga);  // FPGA Bitstream Loading 
  NyanKeysInit((NyanKeys*)&nyan_keys); // Load up the fast cat IP for access to your keys; happy typing.
#ifdef BITCOIN_MINER_EN
  NyanBitcoinInit(&nyan_bitcoin);     // Load up the bitcoin miner, comment this out or delete to disable. 
#endif
  bool keys_started = false;
  uint32_t fpga_retry_ms = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    NyanWatchdogFeed();
    NyanViaProcess(); // Handle any pending VIA raw HID command (main loop context)
    NyanOsProcess(&nos); // Console: parse input, run commands and uploads, send output
    NyanKeysSaveSettings((NyanKeys*)&nyan_keys); // Persist an FN + WIN toggle made in the key frame IRQ
    NyanKeysSendPendingReport(); // Queue a key change that arrived while the HID endpoint was busy

    if (nos.dfu_mode) {
      HAL_GPIO_WritePin(Nyan_DFU_Enable_GPIO_Port, Nyan_DFU_Enable_Pin, GPIO_PIN_SET);
      // Charge the BOOT0 capacitor - keep flushing the console meanwhile
      uint32_t dfu_start = HAL_GetTick();
      while ((HAL_GetTick() - dfu_start) < 1000U) {
        NyanWatchdogFeed();
        NyanCdcTX(&nos);
      }
      NVIC_SystemReset();
    }

    if (nos_fpga.reconfigure_requested) {
      // A new bitstream was written: stop the keys (all reported released),
      // let that report go out, then reload the FPGA
      nos_fpga.reconfigure_requested = false;
      if (keys_started) {
        keys_started = false;
        NyanKeysStop((NyanKeys*)&nyan_keys);
        uint32_t flush_start = HAL_GetTick();
        while (nyan_keys.report_pending && (HAL_GetTick() - flush_start) < 20U)
          NyanKeysSendPendingReport();
      }
      FPGAInit(&nos_fpga);
      fpga_retry_ms = HAL_GetTick();
    } else if (!keys_started) {
      if (nos_fpga.configured) {
        keys_started = true;
        NyanKeysStart((NyanKeys*)&nyan_keys);
      } else if ((HAL_GetTick() - fpga_retry_ms) >= 2000U) {
        // FPGA configuration is retried in the background - paced so that a board
        // whose FPGA never configures keeps full USB/VIA/CDC responsiveness
        // (FPGAInit blocks for seconds while it reloads the bitstream).
        FPGAInit(&nos_fpga);
        fpga_retry_ms = HAL_GetTick(); // pace from the END of the attempt so the main loop gets service time between retries
      }
    }
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 432;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_7) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/**
 * Build and queue the HID report for the latest key state, but only once the
 * keyboard endpoint is idle. USBD_HID_Keyboard_SendReport() silently drops a
 * report while the previous one is in flight (and the endpoint reads the
 * buffer at transmit time), so a change is kept pending until it can be sent.
 * Caller must keep the key frame IRQ from running concurrently.
 */
static void NyanKeysTrySendReport(void)
{
  USBD_HID_Keyboard_HandleTypeDef *hhid = (USBD_HID_Keyboard_HandleTypeDef *)hUsbDevice.pClassData_HID_Keyboard;

  if(!nyan_keys.report_pending || hhid == NULL ||
     hUsbDevice.dev_state != USBD_STATE_CONFIGURED || hhid->state != KEYBOARD_HID_IDLE)
    return;

  NyanBuildHidReportFromKeyStates((NyanKeys*)&nyan_keys, &nyan_hid_report);
  nyan_keys.report_pending = false;
  USBD_HID_Keyboard_SendReport(&hUsbDevice, (uint8_t*)&nyan_hid_report, sizeof(nyan_hid_report));
}

// Key frame interrupt context: a validated (and already acked) frame arrived
void NyanKeysFrameCallback(NyanKeys *keys)
{
  HAL_GPIO_WritePin(GPIOD, Nyan_Keys_LED1_Pin, GPIO_PIN_SET);
  // Increase the performance counter (frames per second)
  nos.perf_keys_count_spi_calls_nxt++;
  // Periodic refresh frames repeat the same state - only report changes
  if(memcmp((uint8_t*)&keys->key_states[0], (uint8_t*)&keys->key_states_prv[0], sizeof(keys->key_states)) != 0) {
    memcpy((uint8_t*)&keys->key_states_prv[0], (uint8_t*)&keys->key_states[0], sizeof(keys->key_states));
    keys->report_pending = true;
  }
  NyanKeysTrySendReport();
  HAL_GPIO_WritePin(GPIOD, Nyan_Keys_LED1_Pin, GPIO_PIN_RESET);
}

// Main loop context: retry a report that could not be queued from the IRQ
void NyanKeysSendPendingReport(void)
{
  if(!nyan_keys.report_pending)
    return;
  HAL_NVIC_DisableIRQ(DMA1_Stream3_IRQn);
  NyanKeysTrySendReport();
  HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
}

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *I2cHandle)
{
  nos_eeprom.tx_inflight = false;
}


void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *I2cHandle)
{
  nos_eeprom.rx_inflight = false;
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  MX_SPI2_Init(); //Upon error in the SPI transmission; reset the SPI2 instance.
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *I2cHandle)
{
  // NAK, bus error, arbitration loss, ... - the EEPROM driver recovers the bus and retries
  nos_eeprom.error = true;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM1) {
    HAL_GPIO_WritePin(GPIOD, Nyan_Keys_LED4_Pin, GPIO_PIN_SET);
    nos_fpga.configured = HAL_GPIO_ReadPin(Nyan_FPGA_Config_Done_GPIO_Port, Nyan_FPGA_Config_Done_Pin);
    // FPGA configuration done indicator
    if(nos_fpga.configured)
      HAL_GPIO_WritePin(Nyan_Keys_LED0_GPIO_Port, Nyan_Keys_LED0_Pin, GPIO_PIN_SET);
    else
      HAL_GPIO_WritePin(Nyan_Keys_LED0_GPIO_Port, Nyan_Keys_LED0_Pin, GPIO_PIN_RESET);
  } if (htim->Instance == TIM6) {
    // Increment the power on pulsing LED angle [sin^2(x) + cos^2(x) = 1]
    system_status_led_angle += SYSTEM_STATUS_DEGREE_INCREMENT;
  } if (htim->Instance == TIM7) {
    // Welcome MoTD guarding from double displays
    if(nos.send_welcome_screen_guard > 0 && ++nos.send_welcome_screen_guard > _NYAN_WELCOME_GUARD_TIME) {
      nos.send_welcome_screen_guard = 0;
    }
  } if (htim->Instance == TIM14) {
    // 1 second period timer. Used for performance metrics
    nos.perf_keys_count_spi_calls = nos.perf_keys_count_spi_calls_nxt;
    nos.perf_keys_count_spi_calls_nxt = 0;
  }
}

void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM1) {
    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
      // Pulse the SystemStatus LED off
      HAL_GPIO_WritePin(Nyan_Keys_LED4_GPIO_Port, Nyan_Keys_LED4_Pin, GPIO_PIN_RESET);
      // Now lets set the new capture compare register value.
      unsigned char cc_val = getSystemStatusOCRValue(system_status_led_angle);
      __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, (unsigned int)cc_val);
      __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_2, (unsigned int)cc_val);
    }
    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2) {
      HAL_GPIO_WritePin(Nyan_Keys_LED0_GPIO_Port, Nyan_Keys_LED0_Pin, GPIO_PIN_RESET);
    }
  }
  if (htim->Instance == TIM8) {
    // The console itself runs in the main loop (NyanOsProcess)
    // Turn off the RX CDC LED
    HAL_GPIO_WritePin(Nyan_Keys_LED3_GPIO_Port, Nyan_Keys_LED3_Pin, GPIO_PIN_RESET);
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  // Record the caller for getinfo, turn on all LEDs and reset Nyan Keys
  NyanFaultRecord(NULL, NYAN_FAULT_ERROR_HANDLER, (uint32_t)__builtin_return_address(0));
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     example: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
