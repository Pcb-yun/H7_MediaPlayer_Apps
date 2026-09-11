/**
 * @file lvgl_port.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief LVGL用户接口源文件
 */

#include "lvgl_port.h"
#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"
#include "Events.h"

static bool is_init = false;

/**
 * @brief LVGL用户初始化函数
 */
bool lvgl_port_init(void) {





    is_init = true;
    return true;
}

/**
 * @brief LVGL主程序
 */
void lvgl_Task(void *argument) {
    (void)argument;

    osEventFlagsWait(System_StatusHandle, SYS_INIT_COMPLETE, osFlagsNoClear, osWaitForever);
    if(!is_init) vTaskDelete(NULL);

    for(;;) {
        uint32_t next = lv_task_handler();

        osDelay(next);
    }
}
