/**
 * @file shell_port.c
 * @author Letter (NevermindZZT@gmail.com)
 * @brief
 * @version 0.1
 * @date 2019-02-22
 *
 * @copyright (c) 2019 Letter
 *
 */

#include "FreeRTOS.h"
#include "task.h"
#include "Events.h"
#include "shell_port.h"
#include "log.h"
#include "usart.h"
#include "string.h"
#include <stdbool.h>

Shell shell;
char shellBuffer[SHELL_BUFFER_SIZE] __attribute__((section(".DTCM")));

/**
 * @brief 用户shell写
 * @param data 数据
 * @param len 数据长度
 * @return 实际写入的数据长度
 */
static short userShellWrite(char *data, uint32_t len) {
    USART1_Transmit_DMA((uint8_t *)data, len);
    return len;
}

/**
* @brief 用户shell读
* @param data 数据缓冲区
* @param len 数据长度
* @return 实际读取的数据长度
*/
static short userShellRead(char *data, unsigned short len) {
    return (short)usart1_rx_stream_read((uint8_t *)data, len);
}

/**
* @brief 用户shell初始化
*/
void userShellInit(void) {
    shell.write = userShellWrite;
    shell.read = userShellRead;
    shellInit(&shell, shellBuffer, SHELL_BUFFER_SIZE);
}

/**
* @brief 用户shell任务
* @param argument 任务参数
*/
void Shell_Task(void *argument) {
    (void)argument;

    osEventFlagsWait(System_StatusHandle, SYS_INIT_COMPLETE, osFlagsNoClear, osWaitForever);

    char data;
    for(;;) {
        osDelay(5);
        // 优先将数据发送给需要使用串口的应用程序
        if(osEventFlagsGet(System_StatusHandle) & APP_NEED_USART) continue;
        if(shell.read(&data, 1)) {
            shellHandler(&shell, data);
        }
    }
}

/**
 * @brief 在线检查任务
 */
void Online_Check_Task(void *argument) {
    (void)argument;

    osEventFlagsWait(System_StatusHandle, SYS_INIT_COMPLETE, osFlagsNoClear, osWaitForever);
    osEventFlagsSet(System_StatusHandle, SHELL_ONLINE);

#if SHELL_USE_ONLINE_CHECK
    const uint8_t Response[] = ONLINE_CHECK_RESPONSE;
    const uint8_t Response_Len = sizeof(Response) - 1;

    uint8_t Temp_buf[64] = {0};
    uint8_t Temp_len = 0;
    uint8_t data;
    int8_t Verify_start = -1;
    static uint8_t retry = 0;

    for (;;) {
        osDelay(ONLINE_CHECK_TIME);
        if((osEventFlagsGet(System_StatusHandle) & APP_NEED_USART)) continue;
        osEventFlagsSet(System_StatusHandle, APP_NEED_USART);

        shell.write((char[]){0x05}, 1); // 查询指令(ENQ)0x05

        uint32_t start_time = osKernelGetTickCount();
        while (1) {
            if (osKernelGetTickCount() - start_time > ONLINE_CHECK_TIMEOUT) {
                if (retry++ >= ONLINE_CHECK_RETRY) {
                    osEventFlagsClear(System_StatusHandle, SHELL_ONLINE);
                    retry = 0; break;
                }
            }

            if (usart1_rx_stream_read((uint8_t *)&data, 1) != 1) {
                osDelay(1); continue;
            }

            if (Temp_len >= sizeof(Temp_buf)) continue;
            Temp_buf[Temp_len++] = data;

            if (data == Response[0]) {
                Verify_start = Temp_len - 1;
            }

            if (Verify_start != -1 && Verify_start + Response_Len <= Temp_len) {
                if (memcmp(&Temp_buf[Verify_start], Response, Response_Len) == 0) {
                    Temp_len -= Response_Len;

                    if (!(osEventFlagsGet(System_StatusHandle) & SHELL_ONLINE)) {
                        osEventFlagsSet(System_StatusHandle, SHELL_ONLINE);
                        shell.parser.length = 0;
                        shell.parser.cursor = 0;
                        Shell_New_Convo(&shell);
                    }
                    break;
                } else {
                    Verify_start = -1;
                }
            }
        }

        usart1_rx_stream_write((uint8_t *)&Temp_buf[0], Temp_len);

        Temp_len = 0;
        osEventFlagsClear(System_StatusHandle, APP_NEED_USART);
    }
#else
    vTaskDelete(NULL);
#endif
}
