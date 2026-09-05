#ifndef __spi_lcd
#define __spi_lcd

#include "tim.h"

/**
 * @brief LCD初始化结构体
 */
typedef struct {
	SPI_HandleTypeDef *hspi;            // SPI句柄
	// MDMA_HandleTypeDef *hmdma;          // MDMA句柄
	DMA2D_HandleTypeDef *hdma2d;        // DMA2D句柄
	uint16_t width;                     // 屏幕宽度
	uint16_t height;                    // 屏幕高度
} LCD_InitTypeDef;

/**
 * @brief LCD句柄结构体
 */
typedef struct {
	LCD_InitTypeDef init;               // 初始化配置
	uint32_t Color;                     // 画笔颜色
	uint32_t BackColor;                 // 背景颜色
	uint8_t ShowNum_Mode;               // 数字显示模式
	uint8_t Direction;                  // 显示方向
	uint8_t X_Offset;                   // X坐标偏移
	uint8_t Y_Offset;                   // Y坐标偏移
} LCD_HandleTypeDef;

#define LCD_MAX_DELAY 300   // 最大等待时间，单位毫秒

#define LCD_Width     240		// LCD的像素长度
#define LCD_Height    240		// LCD的像素宽度

// 显示方向参数
#define	Direction_H			0			//LCD横屏显示
#define	Direction_H_Flip	   1			//LCD横屏显示,上下翻转
#define	Direction_V			2			//LCD竖屏显示
#define	Direction_V_Flip	   3			//LCD竖屏显示,上下翻转

/*---------------------------------------- 常用颜色 ----------------------------------------*/

#define 	LCD_WHITE       0xFFFFFF	 // 纯白色
#define 	LCD_BLACK       0x000000    // 纯黑色

#define 	LCD_BLUE        0x0000FF	 //	纯蓝色
#define 	LCD_GREEN       0x00FF00    //	纯绿色
#define 	LCD_RED         0xFF0000    //	纯红色
#define 	LCD_CYAN        0x00FFFF    //	蓝绿色
#define 	LCD_MAGENTA     0xFF00FF    //	紫红色
#define 	LCD_YELLOW      0xFFFF00    //	黄色
#define 	LCD_GREY        0x2C2C2C    //	灰色

#define 	LIGHT_BLUE      0x8080FF    //	亮蓝色
#define 	LIGHT_GREEN     0x80FF80    //	亮绿色
#define 	LIGHT_RED       0xFF8080    //	亮红色
#define 	LIGHT_CYAN      0x80FFFF    //	亮蓝绿色
#define 	LIGHT_MAGENTA   0xFF80FF    //	亮紫红色
#define 	LIGHT_YELLOW    0xFFFF80    //	亮黄色
#define 	LIGHT_GREY      0xA3A3A3    //	亮灰色

#define 	DARK_BLUE       0x000080    //	暗蓝色
#define 	DARK_GREEN      0x008000    //	暗绿色
#define 	DARK_RED        0x800000    //	暗红色
#define 	DARK_CYAN       0x008080    //	暗蓝绿色
#define 	DARK_MAGENTA    0x800080    //	暗紫红色
#define 	DARK_YELLOW     0x808000    //	暗黄色
#define 	DARK_GREY       0x404040    //	暗灰色

/*------------------------------------------------ 函数声明 ----------------------------------------------*/

void LCD_Init(void) __attribute__((section(".itcm")));   // 初始化LCD

void LCD_SetAddress(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2) __attribute__((section(".itcm")));	// 设置坐标
void LCD_SetColor(uint32_t Color) __attribute__((section(".itcm"))); 				   //	设置画笔颜色
void LCD_SetBackColor(uint32_t Color) __attribute__((section(".itcm")));  				//	设置背景颜色
void LCD_SetDirection(uint8_t direction) __attribute__((section(".itcm")));  	      //	设置显示方向

/*--------------------------------------------- LCD其它引脚 -----------------------------------------------*/

extern TIM_HandleTypeDef htim23;

#define	LCD_Backlight_OFF		HAL_TIM_PWM_Stop(&htim23, TIM_CHANNEL_1);	// 低电平，关闭背光
#define LCD_Backlight_ON		HAL_TIM_PWM_Start(&htim23, TIM_CHANNEL_1);		// 高电平，开启背光
#define	LCD_DC_Command		   HAL_GPIO_WritePin(GPIOG, GPIO_PIN_15, GPIO_PIN_RESET);	   // 低电平，指令传输
#define LCD_DC_Data		      HAL_GPIO_WritePin(GPIOG, GPIO_PIN_15, GPIO_PIN_SET);		// 高电平，数据传输

/*--------------------------------------------- 屏幕指令 ----------------------------------------------*/

// 基础控制指令
#define LCD_SWRESET       0x01              // 软件复位 (Software Reset)
#define LCD_RDDID         0x04              // 读取显示器ID (Read Display ID)
#define LCD_RDDST         0x09              // 读取显示器状态 (Read Display Status)

// 睡眠模式 (Sleep In/Out)
#define LCD_SLEEP_IN      0x10              // 睡眠进入
#define LCD_SLEEP_OUT     0x11              // 睡眠退出

// 部分显示模式 (Partial Mode)
#define LCD_PARTIAL_ON    0x12              // 部分显示开启
#define LCD_PARTIAL_OFF   0x13              // 部分显示关闭

// 显示反转 (Inversion Control)
#define LCD_INVERSION_OFF 0x20              // 显示反转关闭
#define LCD_INVERSION_ON  0x21              // 显示反转开启

// 显示开关 (Display On/Off)
#define LCD_DISPLAY_OFF   0x28              // 显示关闭
#define LCD_DISPLAY_ON    0x29              // 显示开启

// 地址设置指令
#define LCD_CASET         0x2A              // 列地址设置 (Column Address Set)
#define LCD_RASET         0x2B              // 行地址设置 (Row Address Set)
#define LCD_PTLAR         0x30              // 部分区域设置 (Partial Area)

// 内存访问指令
#define LCD_RAMWR         0x2C              // 内存写入 (Memory Write)
#define LCD_RAMRD         0x2E              // 内存读取 (Memory Read)
#define LCD_MADCTL        0x36              // 内存数据访问控制 (Memory Data Access Control)

// 撕裂效果控制 (Tearing Effect Line)
#define LCD_TEARING_ON    0x35              // 撕裂效果开启
#define LCD_TEARING_OFF   0x34              // 撕裂效果关闭

// 空闲模式 (Idle Mode)
#define LCD_IDLE_ON       0x39              // 空闲模式开启
#define LCD_IDLE_OFF      0x38              // 空闲模式关闭
#define LCD_COLMOD        0x3A              // 接口像素格式 (Interface Pixel Format)

// 帧率控制 (Frame Rate Control)
#define LCD_FRMCTR1       0xB1              // 帧率控制1
#define LCD_FRMCTR2       0xB2              // 帧率控制2
#define LCD_FRMCTR3       0xB3              // 帧率控制3

// 显示控制指令
#define LCD_INVCTR        0xB4              // 显示反转控制 (Display Inversion Control)
#define LCD_DISSET5       0xB6              // 显示功能控制 (Display Function Control)
#define LCD_RAMCTRL       0xB0              // 内存模式控制 (Memory Mode Control)

// 电源控制 (Power Control)
#define LCD_PWCTR1        0xC0              // 电源控制1
#define LCD_PWCTR2        0xC1              // 电源控制2
#define LCD_PWCTR3        0xC2              // 电源控制3
#define LCD_PWCTR4        0xC3              // 电源控制4
#define LCD_PWCTR5        0xC4              // 电源控制5

// VCOM控制
#define LCD_VMCTR1        0xC5              // VCOM 控制 (VCOM Control)
#define LCD_VMOFCTR       0xC7              // VCOM 偏移控制 (VCOM Offset Control)

// 伽马控制 (Gamma Control)
#define LCD_GAMSET        0x26              // 伽马设置 (Gamma Set)
#define LCD_GMCTRP1       0xE0              // 正极伽马校正 (Positive Gamma Correction)
#define LCD_GMCTRN1       0xE1              // 负极伽马校正 (Negative Gamma Correction)

// 亮度控制 (Brightness Control)
#define LCD_WRDISBV       0x51              // 写显示亮度 (Write Display Brightness)
#define LCD_RDDISBV       0x52              // 读显示亮度 (Read Display Brightness)
#define LCD_WRCABC        0x55              // 写内容自适应亮度控制 (Write Content Adaptive Brightness Control)
#define LCD_RDCABC        0x56              // 读内容自适应亮度控制 (Read Content Adaptive Brightness Control)

// ID读取指令 (Read ID Commands)
#define LCD_RDID1         0xDA              // 读取 ID1
#define LCD_RDID2         0xDB              // 读取 ID2
#define LCD_RDID3         0xDC              // 读取 ID3
#define LCD_RDID4         0xDD              // 读取 ID4

// 扩展和特殊指令
#define LCD_EXTCTRL       0xF0              // 扩展命令集 (Enable/Disable Extended Command Set)
#define LCD_RESERVED1     0xF8              // 保留指令1
#define LCD_RESERVED2     0xF9              // 保留指令2
#define LCD_TEST_MODE     0xFE              // 测试模式 (Test Mode)
#define LCD_DEEP_SLEEP    0xDE              // 深度睡眠模式 (Deep Sleep Mode)


#endif // __LCD_SPI_154_H__
