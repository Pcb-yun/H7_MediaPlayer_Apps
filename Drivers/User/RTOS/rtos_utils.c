/**
 * @file rtos_utils.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief RTOS实用命令集源文件
 */

#include "rtos_utils.h"
#include "shell.h"
#include "log.h"
#include "cmsis_os2.h"
#include "stm32h7xx_hal.h"
#include "Events.h"
#include "boot.h"
#include "fatfs.h"
#include "dts.h"
#include <string.h>


/**
 * @brief 显示内存分配信息
 */
static void Memory_Info(void) {
    HeapStats_t xHeapStats;
    vPortGetHeapStats(&xHeapStats);

    logPrintln("Free heap: %u bytes\r\n"                /* 当前可用的堆总大小——这是所有空闲块的总和，而不是可以分配的最大块 */
               "Min ever free: %u bytes\r\n"            /* 自系统启动以来堆中可用内存的最小总量（所有可用块的总和） */
               "Largest free block: %u bytes\r\n"       /* 调用vPortGetHeapStats()时堆中所有空闲块的最大大小（以字节为单位） */
               "Smallest free block: %u bytes\r\n"      /* 调用vPortGetHeapStats()时堆中所有空闲块的最小大小（以字节为单位） */
               "Number of free blocks: %u\r\n"          /* 调用vPortGetHeapStats()时堆中所有空闲块的数量 */
               "Successful allocations: %u\r\n"         /* 成功调用pvPortMalloc()的次数 */
               "Successful frees: %u",                  /* 成功调用vPortFree()的次数 */
               xHeapStats.xAvailableHeapSpaceInBytes,
               xHeapStats.xMinimumEverFreeBytesRemaining,
               xHeapStats.xSizeOfLargestFreeBlockInBytes,
               xHeapStats.xSizeOfSmallestFreeBlockInBytes,
               xHeapStats.xNumberOfFreeBlocks,
               xHeapStats.xNumberOfSuccessfulAllocations,
               xHeapStats.xNumberOfSuccessfulFrees);
}

/**
 * @brief 显示任务状态信息
 */
static void Task_Info(void) {
    osThreadId_t task_ids[32];
    uint8_t task_count = osThreadEnumerate(task_ids, 32);

    // 获取总运行时间
    uint32_t ulTotalRunTime;

    // 获取任务数量
    uint32_t ulArraySize = uxTaskGetNumberOfTasks();
    TaskStatus_t *pxTaskStatusArray = pvPortMalloc(ulArraySize * sizeof(TaskStatus_t));

    if(pxTaskStatusArray != NULL) {
        ulArraySize = uxTaskGetSystemState(pxTaskStatusArray, ulArraySize, &ulTotalRunTime);

        // 定义任务信息结构体用于缓存
        typedef struct {
            osThreadId_t task_id;
            const char *task_name;
            osThreadState_t state;
            osPriority_t priority;
            uint32_t stack_space;
            uint32_t high_water;
            float cpu_usage;
        } TaskInfo_t;

        TaskInfo_t *task_info_array = pvPortMalloc(task_count * sizeof(TaskInfo_t));
        if(task_info_array != NULL) {
            // 缓存所有任务信息
            for(uint32_t i = 0; i < task_count; i++) {
                task_info_array[i].task_id = task_ids[i];
                task_info_array[i].task_name = osThreadGetName(task_ids[i]);
                task_info_array[i].state = osThreadGetState(task_ids[i]);
                task_info_array[i].priority = osThreadGetPriority(task_ids[i]);
                task_info_array[i].stack_space = osThreadGetStackSpace(task_ids[i]);

                // 获取任务高水位线和运行时间
                TaskStatus_t xTaskStatus;
                TaskHandle_t xTask = (TaskHandle_t)task_ids[i];
                vTaskGetInfo(xTask, &xTaskStatus, pdTRUE, eInvalid);
                task_info_array[i].high_water = xTaskStatus.usStackHighWaterMark;

                // 计算CPU使用率
                if(ulTotalRunTime > 0) {
                    task_info_array[i].cpu_usage = (float)xTaskStatus.ulRunTimeCounter / (float)ulTotalRunTime * 100.0f;
                } else {
                    task_info_array[i].cpu_usage = 0.0f;
                }
            }

            // 按照CPU占用率从高到低排序（冒泡排序）
            for(uint8_t i = 0; i < task_count - 1; i++) {
                for(uint8_t j = 0; j < task_count - 1 - i; j++) {
                    if(task_info_array[j].cpu_usage < task_info_array[j + 1].cpu_usage) {
                        TaskInfo_t temp = task_info_array[j];
                        task_info_array[j] = task_info_array[j + 1];
                        task_info_array[j + 1] = temp;
                    }
                }
            }

            // 打印排序后的任务信息
            logPrintln("ID        Name              State       Priority    Stack Space    High Water    CPU\r\n"
                       "----------------------------------------------------------------------------------------");

            for(uint8_t i = 0; i < task_count; i++) {
                const char *state_str;
                switch(task_info_array[i].state) {
                    case osThreadInactive: state_str = "Inactive"; break;
                    case osThreadReady: state_str = "Ready"; break;
                    case osThreadRunning: state_str = "Running"; break;
                    case osThreadBlocked: state_str = "Blocked"; break;
                    case osThreadTerminated: state_str = "Terminated"; break;
                    default: state_str = "Unknown"; break;
                }
                logPrintln("%p  %-16s  %-10s  %-10lu  %-10lu     %-12lu  %6.2f %%",
                          task_info_array[i].task_id, task_info_array[i].task_name ? task_info_array[i].task_name : "<unknown>",
                          state_str, task_info_array[i].priority, task_info_array[i].stack_space,
                          task_info_array[i].high_water, task_info_array[i].cpu_usage);
            }
            vPortFree(task_info_array);
        } else {
            logPrintln("Failed to allocate memory for task info array");
        }
        vPortFree(pxTaskStatusArray);
    } else {
        logPrintln("Failed to allocate memory for task status array");
    }
}

/**
 * @brief 显示系统时间信息
 */
static void Time_Info(void) {
    uint32_t tick_count = osKernelGetTickCount();
    uint32_t tick_freq = osKernelGetTickFreq();

    uint8_t hours = tick_count / (tick_freq * 3600);
    uint8_t minutes = (tick_count % (tick_freq * 3600)) / (tick_freq * 60);
    uint8_t seconds = (tick_count % (tick_freq * 60)) / tick_freq;
    uint16_t milliseconds = (tick_count % tick_freq) * 1000 / tick_freq;

    logPrintln("Tick Frequency: %lu Hz\r\n"
            "System Tick: %lu\r\n"
            "Uptime: %02lu:%02lu:%02lu.%03lu",
            tick_freq, tick_count, hours,
            minutes, seconds, milliseconds);

    uint32_t sysclk = HAL_RCC_GetSysClockFreq();
    uint32_t hclk = HAL_RCC_GetHCLKFreq();
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();

    logPrintln("System Clock: %lu Hz\r\n"
            "HCLK: %lu Hz\r\n"
            "PCLK1: %lu Hz\r\n"
            "PCLK2: %lu Hz",
            sysclk, hclk, pclk1, pclk2);
}

/**
 * @brief 显示互斥锁信息
 */
static void Mutex_Info(void) {
    logPrintln("Mutex information:\r\n"
        "ID        Name                  Owner         State\r\n"
        "---------------------------------------------------------");

    extern osMutexId_t Sem_ShellsendHandle;

    if (Sem_ShellsendHandle != NULL) {
        osThreadId_t owner = osMutexGetOwner(Sem_ShellsendHandle);
        const char *owner_name = owner ? osThreadGetName(owner) : "none";
        logPrintln("%p  %-20s  %-12s  %s",
                   Sem_ShellsendHandle, "Sem_Shellsend",
                   owner_name ? owner_name : "<unknown>", owner ? "Locked" : "Unlocked");
    }

}

/**
 * @brief 显示事件标志信息
 */
static void Event_Info(void) {
    logPrintln("Event Flags information:\r\n"
        "ID        Name                  Flags\r\n"
        "------------------------------------------");

    extern osEventFlagsId_t System_StatusHandle;

    if (System_StatusHandle != NULL) {
        uint32_t flags = osEventFlagsGet(System_StatusHandle);
        logPrintln("%p  %-20s  0x%08lx",
                  System_StatusHandle, "System_Status", flags);

        logPrintln("\nFlag Status:");
        logPrintln("%-20s  %s", "SYS_INIT_COMPLETE", (flags & 0x01) ? "SET" : "RESET");
        logPrintln("%-20s  %s", "APP_NEED_USART", (flags & 0x02) ? "SET" : "RESET");
        logPrintln("%-20s  %s", "SHELL_ONLINE", (flags & 0x04) ? "SET" : "RESET");
        logPrintln("%-20s  %s", "FS_MOUNTED", (flags & 0x08) ? "SET" : "RESET");
        logPrintln("%-20s  %s", "USART1_REFRESH", (flags & 0x10) ? "SET" : "RESET");

    }
}

/**
 * @brief 显示消息队列信息
 */
static void Queue_Info(void) {
    logPrintln("Message Queue information:\r\n"
        "ID        Name                  Count/Max    MsgSize\r\n"
        "----------------------------------------------------");

//     extern osMessageQueueId_t LPUart1_Rx_DataHandle;
//
//     if (LPUart1_Rx_DataHandle != NULL) {
//         uint8_t count = osMessageQueueGetCount(LPUart1_Rx_DataHandle);
//         uint8_t capacity = osMessageQueueGetCapacity(LPUart1_Rx_DataHandle);
//         uint16_t msg_size = osMessageQueueGetMsgSize(LPUart1_Rx_DataHandle);
//         logPrintln("%p  %-20s %3lu/%-3lu   %4lu bytes",
//                   LPUart1_Rx_DataHandle, "Usart1_Rx_Data",
//                   count, capacity, msg_size);
//     }

}

/**
 * @brief 持续显示任务信息
 */
static void OS_Top(void) {
    Shell *shell;
    uint8_t byte;
    shell = shellGetCurrent();
    if (shell == NULL) return;

    osEventFlagsSet(System_StatusHandle, APP_NEED_USART);
    uint8_t line = uxTaskGetNumberOfTasks() + 2;
    Task_Info();

    while(1) {
        logPrintln("\033[%dA", line + 1);
        Task_Info();
        if (shell->read((char*)&byte, 1)) {
            if (byte == 0x03) break;
        }
        osDelay(100);
    }
    logPrintln("\033[%dA\033[J\033[2A", line);
    osEventFlagsClear(System_StatusHandle, APP_NEED_USART);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
top, OS_Top, View task information);

/**
 * @brief 手动触发错误
 */
static void OS_Error(void) {
    logPrintln("Manual error trigger");
    Error_Handler();
}

/**
 * @brief 校验文件名是否为 8.3 短文件名格式，且扩展名必须为 hex
 * @param path 待校验文件名
 * @retval 1 合法；0 非法
 */
static uint8_t Is_Valid_Name(const uint8_t *path) {
    const uint8_t *p = path;
    const uint8_t *dot = NULL;    // 扩展名分隔符位置
    uint8_t mainLen = 0;    // 主文件名长度

    if(p == NULL || *p == '\0') {
        return 0;
    }

    // 定位到扩展名分隔符 '.'
    while(*p != '\0' && *p != '.') {
        p++;
    }
    if(*p != '.') {
        return 0;
    }
    dot = p;
    p++;

    // 判断扩展名是否为 hex（忽略大小写，且长度必须为 3）
    if(!((p[0] == 'h' || p[0] == 'H') &&
         (p[1] == 'e' || p[1] == 'E') &&
         (p[2] == 'x' || p[2] == 'X') &&
         p[3] == '\0')) {
        return 0;
    }

    // 判断文件名长度（主文件名 1~8 个字符）
    mainLen = (uint8_t)(dot - path);
    if(mainLen == 0 || mainLen > 8) {
        return 0;
    }

    // 判断文件名有效性（仅允许数字、字母、'_'、'-'）
    for(p = path; p < dot; p++) {
        if(!((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') ||
             (*p >= 'a' && *p <= 'z') || *p == '_' || *p == '-')) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief 系统更新函数
 * @param path 更新文件名称
 */
static void OS_Update(uint8_t *path) {
    if(!Is_Valid_Name(path)) {
        logPrintln("invalid file name, must be 8.3 format");
        return;
    }
    if(!FS_Check()) {
        logPrintln("file system is not mounted");
        return;
    }
    // 以只读方式尝试打开，确认文件存在
    FIL *fp = NULL;
    if(F_open(&fp, path, FA_READ) != FR_OK) {
        logPrintln("file %s not found", path);
        return;
    }
    F_close(&fp);
    BootShared_Update(path);
}

/**
 * @brief 显示系统温度
 */
static void OS_Temp(void) {
    int32_t temperature = 0;

    if(HAL_DTS_Start(&hdts) != HAL_OK) {
        logPrintln("DTS start failed");
        return;
    }

    if(HAL_DTS_GetTemperature(&hdts, &temperature) != HAL_OK) {
        logPrintln("DTS measure failed");
    } else {
        logPrintln("Temperature: %ld degC", temperature);
    }

    HAL_DTS_Stop(&hdts);
}

/**
 * @brief 重启系统
 */
static void OS_Reboot(void) {
    HAL_NVIC_SystemReset();
}

/**
 * @brief OS命令处理函数
 * @param argc 参数数量
 * @param argv 参数列表
 */
static void OS_Tool_Shell(int argc, char *argv[]) {
    if(argc < 2) {
        logPrintln(OS_HELP); return;
    }

    if(strcmp(argv[1], "mem") == 0) {
        Memory_Info();
    } else if(strcmp(argv[1], "task") == 0) {
        Task_Info();
    } else if(strcmp(argv[1], "time") == 0) {
        Time_Info();
    } else if(strcmp(argv[1], "queue") == 0) {
        Queue_Info();
    } else if(strcmp(argv[1], "mutex") == 0) {
        Mutex_Info();
    } else if(strcmp(argv[1], "event") == 0) {
        Event_Info();
    } else if(strcmp(argv[1], "temp") == 0) {
        OS_Temp();
    } else if(strcmp(argv[1], "reboot") == 0) {
        OS_Reboot();
    } else if(strcmp(argv[1], "error") == 0) {
        OS_Error();
    } else if(strcmp(argv[1], "update") == 0) {
        if (argc != 3) logPrintln("Usage: os update PATH");
        OS_Update((uint8_t *)argv[2]);
    } else {
        logPrintln("Invalid command: %s\r\n"
                OS_HELP, argv[1]);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
os, OS_Tool_Shell, System information query tool);
