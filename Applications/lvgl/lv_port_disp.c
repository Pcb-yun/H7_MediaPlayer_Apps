/**
 * @file lv_port_disp_template.c
 *
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"
#include <stdbool.h>

#include "dma2d.h"
#include "spi.h"
#include "mdma.h"
#include "tim.h"
#include "Events.h"
#include "FreeRTOS.h"
#include "task.h"

/*********************
 *      DEFINES
 *********************/
#ifndef MY_DISP_HOR_RES
    #warning Please define or replace the macro MY_DISP_HOR_RES with the actual screen width, default value 320 is used for now.
    #define MY_DISP_HOR_RES    320
#endif

#ifndef MY_DISP_VER_RES
    #warning Please define or replace the macro MY_DISP_VER_RES with the actual screen height, default value 240 is used for now.
    #define MY_DISP_VER_RES    240
#endif

#define BYTE_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565)) /*will be 2 for RGB565 */

#define LCD_DC_CMD HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
#define LCD_DC_DATA HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);

/**********************
 *      TYPEDEFS
 **********************/
#define LCD_BUF_SIZE (MY_DISP_HOR_RES * LV_PORT_DISP_BUF_LINES * BYTE_PER_PIXEL)

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);
static void disp_mdma_config(void);

// static void disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map);

static void disp_send_cmd(lv_display_t *display, const uint8_t *cmd, size_t cmd_size,
                            const uint8_t *param, size_t param_size);
static void disp_send_color(lv_display_t *display, const uint8_t *cmd, size_t cmd_size,
                            uint8_t *param, size_t param_size);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(void)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/
    disp_init();

    /*------------------------------------
     * Create a display and set a flush_cb
     * -----------------------------------*/
//     lv_display_t * disp = lv_display_create(MY_DISP_HOR_RES, MY_DISP_VER_RES);
//     lv_display_set_flush_cb(disp, disp_flush);

    lv_display_t *disp = lv_st7789_create(MY_DISP_HOR_RES, MY_DISP_VER_RES,
                                          LV_LCD_FLAG_NONE,
                                          disp_send_cmd, disp_send_color);
    if (disp == NULL) {
        return;
    }

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_st7789_set_gap(disp, 0U, 0U);
    lv_st7789_set_invert(disp, true);

//     /* Example 1
//      * One buffer for partial rendering*/
//     LV_ATTRIBUTE_MEM_ALIGN
//     static uint8_t buf_1_1[MY_DISP_HOR_RES * 10 * BYTE_PER_PIXEL];            /*A buffer for 10 rows*/
//     lv_display_set_buffers(disp, buf_1_1, NULL, sizeof(buf_1_1), LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* Example 2
     * Two buffers for partial rendering
     * In flush_cb DMA or similar hardware should be used to update the display in the background.*/
    LV_ATTRIBUTE_MEM_ALIGN
    static __attribute__((section(".RAM_D2"), aligned(32)))
    uint8_t buf_2_1[MY_DISP_HOR_RES * LV_PORT_DISP_BUF_LINES * BYTE_PER_PIXEL];

    LV_ATTRIBUTE_MEM_ALIGN
    static __attribute__((section(".RAM_D2"), aligned(32)))
    uint8_t buf_2_2[MY_DISP_HOR_RES * LV_PORT_DISP_BUF_LINES * BYTE_PER_PIXEL];
    lv_display_set_buffers(disp, buf_2_1, buf_2_2, sizeof(buf_2_1), LV_DISPLAY_RENDER_MODE_PARTIAL);

//     /* Example 3
//      * Two buffers screen sized buffer for double buffering.
//      * Both LV_DISPLAY_RENDER_MODE_DIRECT and LV_DISPLAY_RENDER_MODE_FULL works, see their comments*/
//     LV_ATTRIBUTE_MEM_ALIGN
//     static uint8_t buf_3_1[MY_DISP_HOR_RES * MY_DISP_VER_RES * BYTE_PER_PIXEL];
//
//     LV_ATTRIBUTE_MEM_ALIGN
//     static uint8_t buf_3_2[MY_DISP_HOR_RES * MY_DISP_VER_RES * BYTE_PER_PIXEL];
//     lv_display_set_buffers(disp, buf_3_1, buf_3_2, sizeof(buf_3_1), LV_DISPLAY_RENDER_MODE_DIRECT);

}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    /*You code here*/
    MX_SPI6_Init();
    MX_DMA2D_Init();
    disp_mdma_config();
    MX_TIM23_Init();
    HAL_TIM_PWM_Start(&htim23, TIM_CHANNEL_1);
}

/**
 * @brief 为缓冲区搬运配置MDMA
 * @note 使用字节交换顺便解决颜色格式转换问题
 */
static void disp_mdma_config(void)
{
    hmdma_mdma_channel10_sw_0.Init.Endianness = MDMA_LITTLE_BYTE_ENDIANNESS_EXCHANGE;
    if(HAL_MDMA_Init(&hmdma_mdma_channel10_sw_0) != HAL_OK) {
        Error_Handler();
    }
}

volatile bool disp_flush_enabled = true;

/* Enable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

/* Disable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

// /*Flush the content of the internal buffer the specific area on the display.
//  *`px_map` contains the rendered image as raw pixel map and it should be copied to `area` on the display.
//  *You can use DMA or any hardware acceleration to do this operation in the background but
//  *'lv_display_flush_ready()' has to be called when it's finished.*/
// static void disp_flush(lv_display_t * disp_drv, const lv_area_t * area, uint8_t * px_map)
// {
//     if(disp_flush_enabled) {
//         /*The most simple case (but also the slowest) to put all pixels to the screen one-by-one*/
//
//         int32_t x;
//         int32_t y;
//         for(y = area->y1; y <= area->y2; y++) {
//             for(x = area->x1; x <= area->x2; x++) {
//                 /*Put a pixel to the display. For example:*/
//                 /*put_px(x, y, *px_map)*/
//                 px_map++;
//             }
//         }
//     }
//
//     /*IMPORTANT!!!
//      *Inform the graphics library that you are ready with the flushing*/
//     lv_display_flush_ready(disp_drv);
// }

static __attribute__((section(".DTCM")))
TaskHandle_t mdma_task_handle = NULL;

static __attribute__((section(".DTCM")))
lv_display_t *disp = NULL;

/**
 * @brief 屏幕刷新完成回调函数
 */
__attribute__((section(".ITCM")))
void disp_refresh_cplt(void)
{
    lv_display_flush_ready(disp);
}

static __attribute__((section(".ITCM")))
void mdma_cplt(MDMA_HandleTypeDef *_hmdma)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (mdma_task_handle != NULL)
    {
        vTaskNotifyGiveFromISR(mdma_task_handle, &higher_priority_task_woken);
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static __attribute__((section(".ITCM")))
void disp_send_cmd(lv_display_t *display, const uint8_t *cmd, size_t cmd_size,
                          const uint8_t *param, size_t param_size)
{
    LV_UNUSED(display);

    LCD_DC_CMD
    HAL_SPI_Transmit(&hspi6, cmd, (uint16_t)cmd_size, 300U);

    if ((param != NULL) && (param_size != 0U))
    {
        LCD_DC_DATA
        HAL_SPI_Transmit(&hspi6, param, (uint16_t)param_size, 300U);
    }
}

static __attribute__((section(".ITCM")))
void disp_send_color(lv_display_t *display, const uint8_t *cmd, size_t cmd_size,
                            uint8_t *param, size_t param_size)
{
    disp_send_cmd(display, cmd, cmd_size, NULL, 0U);
    disp = display;

    if (!disp_flush_enabled)
    {
        lv_display_flush_ready(display);
        return;
    }

    static __attribute__((section(".RAM_D3"), aligned(32)))
    uint8_t spi_buf[LCD_BUF_SIZE];

    ulTaskNotifyTake(pdTRUE, 0U);
    mdma_task_handle = xTaskGetCurrentTaskHandle();

    HAL_MDMA_RegisterCallback(&hmdma_mdma_channel10_sw_0, HAL_MDMA_XFER_CPLT_CB_ID, mdma_cplt);
    HAL_MDMA_Start_IT(&hmdma_mdma_channel10_sw_0, (uint32_t)param, (uint32_t)spi_buf, (uint32_t)param_size, 1);

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    LCD_DC_DATA
    HAL_SPI_Transmit_DMA(&hspi6, spi_buf, (uint16_t)param_size);
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
