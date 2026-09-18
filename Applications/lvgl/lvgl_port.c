/**
 * @file lvgl_port.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief LVGL用户接口源文件
 */

#include "lvgl_port.h"
#include "lvgl_fatfs.h"
#include "lvgl_jpegdec_port.h"
#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"
#include "dma2d.h"
#include "Events.h"
#include "lv_port_disp.h"
#include "lvgl_user.h"
#include "log.h"
#include <stdint.h>
#include <string.h>

/*
 * heap_4 has no realloc API. Store the payload size immediately before each
 * allocation so LVGL realloc can safely grow a block using pvPortMalloc.
 * The union keeps the returned payload aligned for every Cortex-M7 scalar type.
 */
typedef union {
    size_t size;
    uint64_t align_u64;
    double align_double;
    void *align_ptr;
} lvgl_rtos_alloc_header_t;

static bool is_init = false;

/**
 * @brief LVGL内存初始化函数
 * @note 不实现
 */
void lv_mem_init(void) {

}

/**
 * @brief LVGL内存反初始化函数
 * @note 不实现
 */
void lv_mem_deinit(void) {

}

/**
 * @brief LVGL内存池添加函数
 * @param mem 内存池指针
 * @param bytes 内存池大小
 * @note 不实现
 * @return lv_mem_pool_t 内存池句柄
 */
lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) {
    return NULL;
}

/**
 * @brief LVGL内存池移除函数
 * @note 不实现
 * @param pool 内存池句柄
 */
void lv_mem_remove_pool(lv_mem_pool_t pool) {

}

/**
 * @brief LVGL内存分配函数
 * @param size 内存大小
 * @return void* 内存指针
 */
void *lv_malloc_core(size_t size) {
    lvgl_rtos_alloc_header_t *header;

    if ((size == 0U) || (size > (((size_t)-1) - sizeof(*header)))) {
        return NULL;
    }

    header = pvPortMalloc(sizeof(*header) + size);
    if (header == NULL) {
        return NULL;
    }

    header->size = size;
    return (void *)(header + 1);
}

/**
 * @brief LVGL内存重新分配函数
 * @param ptr 内存指针
 * @param new_size 新内存大小
 * @return void* 新内存指针
 */
void *lv_realloc_core(void *ptr, size_t new_size) {
    lvgl_rtos_alloc_header_t *old_header;
    void *new_ptr;

    if (ptr == NULL) {
        return lv_malloc_core(new_size);
    }
    if (new_size == 0U) {
        lv_free_core(ptr);
        return NULL;
    }

    old_header = ((lvgl_rtos_alloc_header_t *)ptr) - 1;
    if (new_size <= old_header->size) {
        old_header->size = new_size;
        return ptr;
    }

    new_ptr = lv_malloc_core(new_size);
    if (new_ptr == NULL) {
        return NULL;
    }

    memcpy(new_ptr, ptr, old_header->size);
    vPortFree(old_header);
    return new_ptr;
}

/**
 * @brief LVGL内存释放函数
 * @param ptr 内存指针
 */
void lv_free_core(void *ptr) {
    if (ptr != NULL) {
        vPortFree(((lvgl_rtos_alloc_header_t *)ptr) - 1);
    }
}

/**
 * @brief LVGL内存监控函数
 * @param monitor 内存监控结构体指针
 */
void lv_mem_monitor_core(lv_mem_monitor_t *monitor) {
    HeapStats_t stats;
    size_t used;

    if (monitor == NULL) {
        return;
    }

    memset(monitor, 0, sizeof(*monitor));
    vPortGetHeapStats(&stats);

    monitor->total_size = configTOTAL_HEAP_SIZE;
    monitor->free_cnt = stats.xNumberOfFreeBlocks;
    monitor->free_size = stats.xAvailableHeapSpaceInBytes;
    monitor->free_biggest_size = stats.xSizeOfLargestFreeBlockInBytes;
    monitor->used_cnt = stats.xNumberOfSuccessfulAllocations - stats.xNumberOfSuccessfulFrees;
    monitor->max_used = configTOTAL_HEAP_SIZE - stats.xMinimumEverFreeBytesRemaining;

    used = configTOTAL_HEAP_SIZE - stats.xAvailableHeapSpaceInBytes;
    monitor->used_pct = (uint8_t)((used * 100U) / configTOTAL_HEAP_SIZE);
    if (stats.xAvailableHeapSpaceInBytes != 0U) {
        monitor->frag_pct = (uint8_t)(100U -
            ((stats.xSizeOfLargestFreeBlockInBytes * 100U) /
             stats.xAvailableHeapSpaceInBytes));
    }
}

/**
 * @brief LVGL内存测试函数
 * @note 不实现
 * @return lv_result_t 测试结果
 */
lv_result_t lv_mem_test_core(void) {
    return LV_RESULT_OK;
}

/**
 * @brief LVGL日志打印函数
 * @param level 日志级别
 * @param buf 日志缓冲区指针
 */
static void lv_log_print(lv_log_level_t level, const char *buf) {
    switch(level) {
        case LV_LOG_LEVEL_TRACE:
            logPrintln("%s", buf);
            break;
        case LV_LOG_LEVEL_INFO:
            logPrintln("%s", buf);
            break;
        case LV_LOG_LEVEL_WARN:
            logPrintln("%s", buf);
            break;
        case LV_LOG_LEVEL_ERROR:
            logPrintln("%s", buf);
            break;
        case LV_LOG_LEVEL_USER:
            logPrintln("%s", buf);
            break;
        default: break;
    }
}

/**
 * @brief LVGL用户初始化函数
 */
bool lvgl_port_init(void) {
    lv_init();
    if (!lvgl_fatfs_init()) {
        return false;
    }

    lv_tick_set_cb(xTaskGetTickCount);

    MX_DMA2D_Init();
    lv_port_disp_init();
    lv_log_register_print_cb(lv_log_print);

    if (lv_display_get_default() == NULL) {
        return false;
    }

    lvgl_user_init();

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
        uint32_t next = lv_timer_handler();
        osDelay(next);
    }
}
