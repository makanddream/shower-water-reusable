/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include "WS2812.h"
#include "stripEffects.h"
#include "lwow/lwow.h"
#include "lwow/devices/lwow_device_ds18x20.h"
#include "scan_devices.h"
#include "Buttons.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define UPDATE_INTERVAL 	15 //refresh rate: 1/0.015ms = 66Hz
#define TASK_INTERVAL		10000
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim1;
DMA_HandleTypeDef hdma_tim1_ch1;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart1_rx;
DMA_HandleTypeDef hdma_usart1_tx;

/* Definitions for MainTask */
osThreadId_t MainTaskHandle;
const osThreadAttr_t MainTask_attributes = {
  .name = "MainTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 256 * 4
};
/* Definitions for circularRingRed */
osThreadId_t circularRingRedHandle;
const osThreadAttr_t circularRingRed_attributes = {
  .name = "circularRingRed",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 256 * 4
};
/* Definitions for ReadTemperature */
osThreadId_t ReadTemperatureHandle;
const osThreadAttr_t ReadTemperature_attributes = {
  .name = "ReadTemperature",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 256 * 4
};
/* Definitions for circularRingGre */
osThreadId_t circularRingGreHandle;
const osThreadAttr_t circularRingGre_attributes = {
  .name = "circularRingGre",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 256 * 4
};
/* Definitions for errorTask */
osThreadId_t errorTaskHandle;
const osThreadAttr_t errorTask_attributes = {
  .name = "errorTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 256 * 4
};
/* Definitions for errorSignalRing */
osThreadId_t errorSignalRingHandle;
const osThreadAttr_t errorSignalRing_attributes = {
  .name = "errorSignalRing",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 256 * 4
};
/* Definitions for updateTimer */
osTimerId_t updateTimerHandle;
const osTimerAttr_t updateTimer_attributes = {
  .name = "updateTimer"
};
/* Definitions for buttonBinarySem */
osSemaphoreId_t buttonBinarySemHandle;
const osSemaphoreAttr_t buttonBinarySem_attributes = {
  .name = "buttonBinarySem"
};
/* Definitions for readTemperatureBinarySem */
osSemaphoreId_t readTemperatureBinarySemHandle;
const osSemaphoreAttr_t readTemperatureBinarySem_attributes = {
  .name = "readTemperatureBinarySem"
};
/* USER CODE BEGIN PV */
extern const lwow_ll_drv_t lwow_ll_drv_stm32_hal;
lwow_t ow;
lwow_rom_t rom_ids[20];
size_t rom_found;
float actual_temp = 0;
int error_count = 0;
ButtonState buttonState;
bool isAborted = false;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_DMA_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART1_UART_Init(void);
void Main_Task(void *argument);
void CircularRingRedTask(void *argument);
void readTemperatureTask(void *argument);
void CircularRingGreen(void *argument);
void ErrorTask(void *argument);
void ErrorSignalRingLedTask(void *argument);
void vTimerUpdateCallback(void *argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void safeprintf(const char* fmt, ...) {
    va_list va;

    va_start(va, fmt);
    osKernelLock();
    vprintf(fmt, va);
    osKernelUnlock();
    va_end(va);
}

/*void delayWithTimer(int time_s){
  htim2.Init.Prescaler = (uint32_t)((SystemCoreClock / TIMER_CLOCK_FREQ) - 1);
  htim2.Init.Period = (TIMER_CLOCK_FREQ / (1/time_s)) - 1;
}

void Button_Timer_Callback(void){
	HAL_TIM_Base_Stop_IT(&htim2);
	if(HAL_GPIO_ReadPin(controlSwitch_GPIO_Port, controlSwitch_Pin) == GPIO_PIN_RESET){
		printf("Button click\r\n");
		osSemaphoreRelease(buttonBinarySemHandle);
	}

	HAL_NVIC_EnableIRQ(EXTI3_IRQn);
}*/

void Switch_Callback(void){
	HAL_NVIC_DisableIRQ(EXTI3_IRQn);

	buttonState = getButtonState();
	if(buttonState == BUTTON_LONG){
		isAborted = true;
	}

	HAL_NVIC_EnableIRQ(EXTI3_IRQn);
}

uint8_t getButton() {
	return HAL_GPIO_ReadPin(controlSwitch_GPIO_Port, controlSwitch_Pin) == GPIO_PIN_RESET ?
			1 : 0;
}

void water_valve_control(bool activate){
	if(activate){
		printf("Turn on the valve to put the cold water into the cistern");
		HAL_GPIO_WritePin(relayControl_GPIO_Port, relayControl_Pin, GPIO_PIN_RESET);
	}else{
		printf("Turn off the valve to put the water into the normal circuit");
		HAL_GPIO_WritePin(relayControl_GPIO_Port, relayControl_Pin, GPIO_PIN_SET);
	}
}


void Error_Application(void){
	water_valve_control(false);

	osThreadSuspend(ReadTemperatureHandle);
	osThreadSuspend(circularRingRedHandle);
	osThreadSuspend(circularRingGreHandle);

	osThreadResume(errorSignalRingHandle);
	osDelay(7000);
	osThreadSuspend(errorSignalRingHandle);

	fillBufferBlack();

	osSemaphoreAcquire(buttonBinarySemHandle , osWaitForever);
	osThreadTerminate(MainTaskHandle);
	MainTaskHandle = osThreadNew(Main_Task, NULL, &MainTask_attributes);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_DMA_Init();
  MX_TIM1_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  printf("Application running on STM32L412KB-Nucleo board!\r\n");

  /* Init the system */
  water_valve_control(false); //Water valve off
  ws2812_init(&htim1, NULL);
  fillBufferBlack();
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of buttonBinarySem */
  buttonBinarySemHandle = osSemaphoreNew(1, 1, &buttonBinarySem_attributes);

  /* creation of readTemperatureBinarySem */
  readTemperatureBinarySemHandle = osSemaphoreNew(1, 1, &readTemperatureBinarySem_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  osSemaphoreAcquire(buttonBinarySemHandle , osWaitForever);
  osSemaphoreAcquire(readTemperatureBinarySemHandle , osWaitForever);
  /* USER CODE END RTOS_SEMAPHORES */

  /* Create the timer(s) */
  /* creation of updateTimer */
  updateTimerHandle = osTimerNew(vTimerUpdateCallback, osTimerPeriodic, NULL, &updateTimer_attributes);

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  osTimerStart(updateTimerHandle, UPDATE_INTERVAL);
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of MainTask */
  MainTaskHandle = osThreadNew(Main_Task, NULL, &MainTask_attributes);

  /* creation of circularRingRed */
  circularRingRedHandle = osThreadNew(CircularRingRedTask, NULL, &circularRingRed_attributes);

  /* creation of ReadTemperature */
  ReadTemperatureHandle = osThreadNew(readTemperatureTask, NULL, &ReadTemperature_attributes);

  /* creation of circularRingGre */
  circularRingGreHandle = osThreadNew(CircularRingGreen, NULL, &circularRingGre_attributes);

  /* creation of errorTask */
  errorTaskHandle = osThreadNew(ErrorTask, NULL, &errorTask_attributes);

  /* creation of errorSignalRing */
  errorSignalRingHandle = osThreadNew(ErrorSignalRingLedTask, NULL, &errorSignalRing_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  osThreadSuspend(ReadTemperatureHandle);
  osThreadSuspend(circularRingRedHandle);
  osThreadSuspend(circularRingGreHandle);
  osThreadSuspend(errorSignalRingHandle);
  osThreadSuspend(errorTaskHandle);

  /* USER CODE END RTOS_THREADS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);
  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_10;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1|RCC_PERIPHCLK_USART2;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
  PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = (uint32_t)((SystemCoreClock / TIMER_CLOCK_FREQ) - 1);
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = TIMER_PERIOD-1;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = (TIMER_PERIOD * 2 / 3);
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
  /* DMA1_Channel4_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
  /* DMA1_Channel5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(relayControl_GPIO_Port, relayControl_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : controlSwitch_Pin */
  GPIO_InitStruct.Pin = controlSwitch_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(controlSwitch_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : relayControl_Pin */
  GPIO_InitStruct.Pin = relayControl_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(relayControl_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD3_Pin */
  GPIO_InitStruct.Pin = LD3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD3_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI3_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);

}

/* USER CODE BEGIN 4 */
/**
 * \brief           Printf character handler
 * \param[in]       ch: Character to send
 * \param[in]       f: File pointer
 * \return          Written character
 */
#ifdef __GNUC__
int __io_putchar(int ch) {
#else
int fputc(int ch, FILE* fil) {
#endif
    uint8_t c = (uint8_t)ch;
    HAL_UART_Transmit(&huart2, &c, 1, 100);
    return ch;
}
/* USER CODE END 4 */

/* USER CODE BEGIN Header_Main_Task */
/**
  * @brief  Function implementing the MainTask thread.
  * @param  argument: Not used 
  * @retval None
  */
/* USER CODE END Header_Main_Task */
void Main_Task(void *argument)
{
  /* USER CODE BEGIN 5 */
	osThreadSuspend(ReadTemperatureHandle);
	osThreadSuspend(circularRingRedHandle);
	osThreadSuspend(circularRingGreHandle);

#if 0
	for(;;){
		ButtonState buttonState = getButtonState();
		if(buttonState == BUTTON_NONE){
			//printf("buttonState = BUTTON_NONE\r\n");
		}else if(buttonState == BUTTON_SHORT){
			printf("buttonState = BUTTON_SHORT\r\n");
		}else if(buttonState == BUTTON_LONG){
			printf("buttonState = BUTTON_LONG\r\n");
		}
		osDelay(10);
	}
#endif

#if 1
	for(;;){
		if (buttonState== BUTTON_SHORT)
		{
			// Resume the readTemperature task
			osThreadResume(ReadTemperatureHandle);
			//osSemaphoreRelease(readTemperatureBinarySemHandle);

			// Activate the relay to open the valve for the water
			water_valve_control(true);

			// Resume the led ring task
			osThreadResume(circularRingRedHandle);

			while(actual_temp < CORRECT_TEMPERATURE && !isAborted);

			if(!isAborted){
				// Desactivate the relay to close the valve because the temperature of the water is good !
				water_valve_control(false);

				//Suspend temperature en led ring task
				//osThreadSuspend(readTemperatureTask);
				osThreadSuspend(circularRingRedHandle);

				osThreadResume(circularRingGreHandle);

				osDelay(4000);

				osThreadSuspend(circularRingGreHandle);
				fillBufferBlack();
				HAL_NVIC_SystemReset();
			}else{
				// Desactivate the relay to close the valve because the temperature of the water is good !
				water_valve_control(false);

				/*osThreadTerminate(circularRingGreHandle);
				osThreadTerminate(ReadTemperatureHandle);

				osThreadResume(circularRingRedHandle);

				osDelay(4000);

				osThreadSuspend(circularRingRedHandle);
				fillBufferBlack();
				osThreadTerminate(MainTaskHandle);*/
				HAL_NVIC_SystemReset();
			}
		}
	}

	//osSemaphoreAcquire(buttonBinarySemHandle , osWaitForever);
#endif
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_CircularRingRedTask */
/**
* @brief Function implementing the circularRingRed thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_CircularRingRedTask */
void CircularRingRedTask(void *argument)
{
  /* USER CODE BEGIN CircularRingRedTask */
	//stripEffect_CircularRing(50, 255, 0, 0);
	stripEffect_ColorWheel(50);
  /* USER CODE END CircularRingRedTask */
}

/* USER CODE BEGIN Header_readTemperatureTask */
/**
* @brief Function implementing the ReadTemperature thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_readTemperatureTask */
void readTemperatureTask(void *argument)
{
  /* USER CODE BEGIN readTemperatureTask */
  /* Infinite loop */
	//if (readTemperatureBinarySemHandle != NULL)
		//{
		/* Try to obtain the semaphore */
		//if (osSemaphoreAcquire(readTemperatureBinarySemHandle , osWaitForever) == osOK)
		//{
			float avg_temp;
			size_t avg_temp_count;

			/* Initialize 1-Wire library and set user argument to NULL */
			lwow_init(&ow, &lwow_ll_drv_stm32_hal, &huart1);

			/* Get onewire devices connected on 1-wire port */
			do {
				if (scan_onewire_devices(&ow, rom_ids, LWOW_ARRAYSIZE(rom_ids), &rom_found) == lwowOK) {
					printf("Devices scanned, found %d devices!\r\n", (int)rom_found);
				} else {
					printf("Device scan error\r\n");
					error_count++;
					if(error_count >= MAX_ERROR_COUNT){
						osThreadResume(errorTaskHandle);
					}
				}
				if (rom_found == 0) {
					osDelay(1000);
				}
			} while (rom_found == 0);

			if (rom_found > 0) {
				/* Infinite loop */
				actual_temp = 0;
				while (1) {
					printf("Start temperature conversion\r\n");
					lwow_ds18x20_start(&ow, NULL);      /* Start conversion on all devices, use protected API */
					osDelay(1000);                      /* Release thread for 1 second */

					/* Read temperature on all devices */
					avg_temp = 0;
					avg_temp_count = 0;
					for (size_t i = 0; i < rom_found; i++) {
						if (lwow_ds18x20_is_b(&ow, &rom_ids[i])) {
							float temp;
							uint8_t resolution = lwow_ds18x20_get_resolution(&ow, &rom_ids[i]);
							if (lwow_ds18x20_read(&ow, &rom_ids[i], &temp)) {
								printf("Sensor %02u temperature is %d.%d degrees (%u bits resolution)\r\n",
									(unsigned)i, (int)temp, (int)((temp * 1000.0f) - (((int)temp) * 1000)), (unsigned)resolution);

								avg_temp += temp;
								actual_temp = temp;
								avg_temp_count++;
							} else {
								printf("Could not read temperature on sensor %u\r\n", (unsigned)i);

								if(error_count > MAX_ERROR_COUNT){
									osThreadResume(errorTaskHandle);
								}else{
									error_count++;
								}
							}
						}
					}
					if (avg_temp_count > 0) {
						avg_temp = avg_temp / avg_temp_count;
					}
					printf("Average temperature: %d.%d degrees\r\n", (int)avg_temp, (int)((avg_temp * 100.0f) - ((int)avg_temp) * 100));
				}
			}
			printf("Terminating application thread\r\n");
			osThreadExit();
			//}
//}
  /* USER CODE END readTemperatureTask */
}

/* USER CODE BEGIN Header_CircularRingGreen */
/**
* @brief Function implementing the circularRingGre thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_CircularRingGreen */
void CircularRingGreen(void *argument)
{
  /* USER CODE BEGIN CircularRingGreen */
	stripEffect_CircularRing(50, 0, 255, 0);
  /* USER CODE END CircularRingGreen */
}

/* USER CODE BEGIN Header_ErrorTask */
/**
* @brief Function implementing the errorTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_ErrorTask */
void ErrorTask(void *argument)
{
  /* USER CODE BEGIN ErrorTask */
  /* Infinite loop */
	// Desactivate the relay to close the valve because the temperature of the water is good !
	water_valve_control(false);

	osThreadSuspend(ReadTemperatureHandle);
	osThreadSuspend(circularRingRedHandle);
	osThreadSuspend(circularRingGreHandle);

	osThreadResume(errorSignalRingHandle);
	osDelay(5000);
	osThreadSuspend(errorSignalRingHandle);

	osSemaphoreAcquire(buttonBinarySemHandle , osWaitForever);
	osThreadTerminate(MainTaskHandle);
	MainTaskHandle = osThreadNew(Main_Task, NULL, &MainTask_attributes);
  /* USER CODE END ErrorTask */
}

/* USER CODE BEGIN Header_ErrorSignalRingLedTask */
/**
* @brief Function implementing the errorSignalRing thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_ErrorSignalRingLedTask */
void ErrorSignalRingLedTask(void *argument)
{
  /* USER CODE BEGIN ErrorSignalRingLedTask */
  /* Infinite loop */
  stripEffect_HeartBeat(250, 255, 0, 0);
  /* USER CODE END ErrorSignalRingLedTask */
}

/* vTimerUpdateCallback function */
void vTimerUpdateCallback(void *argument)
{
  /* USER CODE BEGIN vTimerUpdateCallback */
  ws2812_update();
  /* USER CODE END vTimerUpdateCallback */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */
  else if(htim->Instance == TIM2) {
	  //Button_Timer_Callback();
  }
  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */

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
void Switch_Callback(void){

}
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/