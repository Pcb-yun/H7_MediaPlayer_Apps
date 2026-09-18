/**
 * @file lvgl_attr.h
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief LVGL 自定义属性宏
 */


// 需要按边界对齐的内存（如绘制缓冲区），例：__attribute__((aligned(4)))
#define LV_ATTRIBUTE_MEM_ALIGN __attribute__((aligned(32)))

// 修饰 lv_tick_inc()，常由定时器中断调用，放入 ITCM 可降低中断延迟
#define LV_ATTRIBUTE_TICK_INC __attribute__((section(".ITCM")))

// 修饰 lv_timer_handler()，可放入 ITCM 等快速内存
#define LV_ATTRIBUTE_TIMER_HANDLER __attribute__((section(".ITCM")))

// 修饰 lv_display_flush_ready()，常由 DMA 或显示中断调用
#define LV_ATTRIBUTE_FLUSH_READY __attribute__((section(".ITCM")))

// 修饰 lv_display_sync_ready()，与 FLUSH_READY 类似，用于中断上下文
#define LV_ATTRIBUTE_SYNC_READY __attribute__((section(".ITCM")))

// 修饰字体位图等大型只读数组，可放入指定 Flash 或外部存储区段
#define LV_ATTRIBUTE_LARGE_CONST __attribute__((section(".W25Q64")))

// // 修饰 RAM 中的大型数组，可放入外部 RAM 等区段
// #define LV_ATTRIBUTE_LARGE_RAM_ARRAY

// 修饰性能关键的函数或数据，放入 ITCM、SRAM 等快速内存
#define LV_ATTRIBUTE_FAST_MEM __attribute__((section(".ITCM")))

// // 全局 extern 数据声明的前缀，可指定数据段或导出属性
// #define LV_ATTRIBUTE_EXTERN_DATA
//
// // 向 MicroPython 等语言绑定导出 LV_<CONST> 常量，不用绑定时留空
// #define LV_EXPORT_CONST_INT
