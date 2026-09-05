/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "tim.h"
#include "Events.h"
#include "message.h"
#include "shell.h"
#include "sdmmc.h"
#include "fatfs.h"

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
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for Sys_Init */
osThreadId_t Sys_InitHandle;
const osThreadAttr_t Sys_Init_attributes = {
  .name = "Sys_Init",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityRealtime7,
};
/* Definitions for Shell */
osThreadId_t ShellHandle;
const osThreadAttr_t Shell_attributes = {
  .name = "Shell",
  .stack_size = 2048 * 4,
  .priority = (osPriority_t) osPriorityNormal2,
};
/* Definitions for Online_Check */
osThreadId_t Online_CheckHandle;
const osThreadAttr_t Online_Check_attributes = {
  .name = "Online_Check",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow3,
};
/* Definitions for System_Status */
osEventFlagsId_t System_StatusHandle;
const osEventFlagsAttr_t System_Status_attributes = {
  .name = "System_Status"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void Sys_Init_Task(void *argument);
extern void Shell_Task(void *argument);
extern void Online_Check_Task(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void configureTimerForRunTimeStats(void);
unsigned long getRunTimeCounterValue(void);
void vApplicationIdleHook(void);
void vApplicationTickHook(void);
void vApplicationStackOverflowHook(xTaskHandle xTask, char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 1 */
/* Functions needed when configGENERATE_RUN_TIME_STATS is on */
__weak void configureTimerForRunTimeStats(void)
{
  MX_TIM24_Init();
}

__weak unsigned long getRunTimeCounterValue(void)
{
  return __HAL_TIM_GET_COUNTER(&htim24);
}
/* USER CODE END 1 */

/* USER CODE BEGIN 2 */
void vApplicationIdleHook( void )
{
   /* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
   to 1 in FreeRTOSConfig.h. It will be called on each iteration of the idle
   task. It is essential that code added to this hook function never attempts
   to block in any way (for example, call xQueueReceive() with a block time
   specified, or call vTaskDelay()). If the application makes use of the
   vTaskDelete() API function (as this demo application does) then it is also
   important that vApplicationIdleHook() is permitted to return to its calling
   function, because it is the responsibility of the idle task to clean up
   memory allocated by the kernel to any task that has since been deleted. */

  // 空闲时LED闪烁
  HAL_GPIO_TogglePin(GPIOG,GPIO_PIN_7);
}
/* USER CODE END 2 */

/* USER CODE BEGIN 3 */
void vApplicationTickHook( void )
{
   /* This function will be called by each tick interrupt if
   configUSE_TICK_HOOK is set to 1 in FreeRTOSConfig.h. User code can be
   added here, but the tick hook is called from an interrupt context, so
   code must not attempt to block, and only the interrupt safe FreeRTOS API
   functions can be used (those that end in FromISR()). */

#if USE_WG
   extern IWDG_HandleTypeDef hiwdg1;
   // 喂狗
   HAL_IWDG_Refresh(&hiwdg1);
#endif
}
/* USER CODE END 3 */

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */

  my_printf("\r\n[ERROR] Stack overflow detected!\r\n");

  my_printf("Task name: %s\r\n", pcTaskName);
  my_printf("Task ID: 0x%p\r\n", xTask);

  Error_Handler();
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
   /* vApplicationMallocFailedHook() will only be called if
   configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
   function that will get called if a call to pvPortMalloc() fails.
   pvPortMalloc() is called internally by the kernel whenever a task, queue,
   timer or semaphore is created. It is also called by various parts of the
   demo application. If heap_1.c or heap_2.c are used, then the size of the
   heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
   FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
   to query the size of free heap space that remains (although it does not
   provide information on how the remaining heap might be fragmented). */

  taskDISABLE_INTERRUPTS();
  my_printf("\r\n[ERROR] Memory allocation failed!\r\n");

  // 获取当前任务信息
  TaskHandle_t xCurrentTask = xTaskGetCurrentTaskHandle();
  if(xCurrentTask != NULL) {
    const char *pcTaskName = pcTaskGetName(xCurrentTask);
    my_printf("Current task: %s\r\n", pcTaskName);
  }

  // 打印空闲堆信息
  size_t xFreeHeapSize = xPortGetFreeHeapSize();
  size_t xMinimumEverFreeHeapSize = xPortGetMinimumEverFreeHeapSize();
  my_printf("Free heap size: %u bytes\r\n", xFreeHeapSize);
  my_printf("Min ever free heap: %u bytes\r\n", xMinimumEverFreeHeapSize);

  Error_Handler();
}
/* USER CODE END 5 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  Show_dmesg(dmesg_wait, "Initialize FreeRTOS.");
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of Sys_Init */
  Sys_InitHandle = osThreadNew(Sys_Init_Task, NULL, &Sys_Init_attributes);

  /* creation of Shell */
  ShellHandle = osThreadNew(Shell_Task, NULL, &Shell_attributes);

  /* creation of Online_Check */
  Online_CheckHandle = osThreadNew(Online_Check_Task, NULL, &Online_Check_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* Create the event(s) */
  /* creation of System_Status */
  System_StatusHandle = osEventFlagsNew(&System_Status_attributes);

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  Show_dmesg(dmesg_ok, NULL);
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_Sys_Init_Task */
/**
  * @brief  Function implementing the Sys_Init thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_Sys_Init_Task */
void Sys_Init_Task(void *argument)
{
  /* USER CODE BEGIN Sys_Init_Task */
#if USE_WG
  SHOW_DMESG(dmesg_wait, "Initialize Watch Dog");
  extern void MX_IWDG1_Init(void);
  MX_IWDG1_Init();
  SHOW_DMESG(dmesg_ok, NULL);
#else
  my_printf("[warn] Debug Mode, Watch Dog Disabled.\r\n");
#endif

  SHOW_DMESG(dmesg_wait, "Initialize Shell");
  extern void userShellInit(void);
  userShellInit();
  SHOW_DMESG(dmesg_ok, NULL);

  SHOW_DMESG(dmesg_wait, "Initialize shell log");
  extern void logInit(void);
  logInit();
  SHOW_DMESG(dmesg_ok, NULL);

  SHOW_DMESG(dmesg_wait, "Initialize SDMMC1");
  if (SD_Init()) {
    SHOW_DMESG(dmesg_ok, NULL);
    SHOW_DMESG(dmesg_wait, "Initialize FATFS");
    MX_FATFS_Init();
    if (FS_Check()) SHOW_DMESG(dmesg_ok, NULL);
    else SHOW_DMESG(dmesg_fail, NULL);
  } else SHOW_DMESG(dmesg_fail, NULL);

  Show_dmesg(dmesg_wait, "Initialize shell file system");
  extern void userShellFsInit(void);
  userShellFsInit();
  Show_dmesg(dmesg_ok, NULL);

  Show_dmesg(dmesg_wait, "Initialize Hardware CRC");
  extern void MX_CRC_Init(void);
  MX_CRC_Init();
  Show_dmesg(dmesg_ok, NULL);

  Show_dmesg(dmesg_wait, "Initialize SAI1");
  extern void MX_SAI1_Init(void);
  MX_SAI1_Init();
  Show_dmesg(dmesg_ok, NULL);

  Show_dmesg(dmesg_wait, "Initialize OCTOSPI1");
  extern void MX_OCTOSPI1_Init(void);
  MX_OCTOSPI1_Init();
  Show_dmesg(dmesg_ok, NULL);

  extern Shell shell;
  Shell_New_Convo(&shell);
  osEventFlagsSet(System_StatusHandle, SYS_INIT_COMPLETE);

	vTaskDelete(NULL);
  /* USER CODE END Sys_Init_Task */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

