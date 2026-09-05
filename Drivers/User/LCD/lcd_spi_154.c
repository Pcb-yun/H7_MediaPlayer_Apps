/***
	*  1.屏幕配置为16位RGB565格式
	*  2.SPI通信速度为 68.75M
***/

#include "lcd_spi_154.h"
#include "spi.h"
#include "dma2d.h"
#include "log.h"
#include "shell.h"
#include <stdlib.h>
#include "cmsis_os2.h"
#include "get_res.h"

// 全局LCD句柄
LCD_HandleTypeDef hlcd;
extern SPI_HandleTypeDef hspi6;
extern DMA2D_HandleTypeDef hdma2d;


/**
 * @brief 写命令函数
 *
 * @param lcd_command 要发送的命令
 */
static void LCD_WriteCommand(uint8_t lcd_command) {
    // while (HAL_SPI_GetState(hlcd.init.hspi) != HAL_SPI_STATE_READY) {
    //     osDelay(1);
    // }
	LCD_DC_Command;
	// if (HAL_SPI_Transmit(hlcd.init.hspi, &lcd_command, 1, LCD_MAX_DELAY) != HAL_OK) {
	// 	logError("SPI6 Transmit Error: %s", SPI_GetErrorString(hlcd.init.hspi->ErrorCode));
	// }
}

/**
 * @brief 写8位数据函数
 *
 * @param lcd_data 要发送的8位数据
 */
static void LCD_WriteData_8bit(uint8_t lcd_data) {
    // while (HAL_SPI_GetState(hlcd.init.hspi) != HAL_SPI_STATE_READY) {
    //     osDelay(1);
    // }
	LCD_DC_Data;
	// if (HAL_SPI_Transmit(hlcd.init.hspi, &lcd_data, 1, LCD_MAX_DELAY) != HAL_OK) {
	// 	logError("SPI6 Transmit Error: %s", SPI_GetErrorString(hlcd.init.hspi->ErrorCode));
	// }
}

/**
 * @brief 写16位数据函数
 *
 * @param lcd_data 要发送的16位数据
 */
static __attribute__((section(".itcm")))
void LCD_WriteData_16bit(uint16_t lcd_data) {
    // while (HAL_SPI_GetState(hlcd.init.hspi) != HAL_SPI_STATE_READY) {
    //     osDelay(1);
    // }
	// uint8_t data[2] = {lcd_data >> 8, lcd_data & 0xFF};
	LCD_DC_Data;
	// if (HAL_SPI_Transmit(hlcd.init.hspi, data, 2, LCD_MAX_DELAY) != HAL_OK) {
	// 	logError("SPI6 Transmit Error: %s", SPI_GetErrorString(hlcd.init.hspi->ErrorCode));
	// }
}

/**
 * @brief SPI LCD初始化函数
 */
static void SPI_LCD_Init(void) {
	HAL_Delay(10);               // 屏幕刚完成复位时（包括上电复位），需要等待5ms才能发送指令
	LCD_WriteCommand(LCD_MADCTL);       // 显存访问控制 指令，用于设置访问显存的方式
	LCD_WriteData_8bit(0x00);     // 配置成 从上到下、从左到右，RGB像素格式

	LCD_WriteCommand(LCD_COLMOD);		// 接口像素格式 指令，用于设置使用 12位、16位还是18位色
	LCD_WriteData_8bit(0x05);     // 此处配置成 16位 像素格式

	// 接下来很多都是电压设置指令，直接使用厂家给设定值
	LCD_WriteCommand(LCD_FRMCTR1);
	LCD_WriteData_8bit(0x0C);
	LCD_WriteData_8bit(0x0C);
	LCD_WriteData_8bit(0x00);
	LCD_WriteData_8bit(0x33);
	LCD_WriteData_8bit(0x33);

	LCD_WriteCommand(LCD_FRMCTR2);		   // 栅极电压设置指令
	LCD_WriteData_8bit(0x35);     // VGH = 13.26V，VGL = -10.43V

	LCD_WriteCommand(LCD_FRMCTR3);		// 公共电压设置指令
	LCD_WriteData_8bit(0x19);     // VCOM = 1.35V

	LCD_WriteCommand(LCD_PWCTR1);
	LCD_WriteData_8bit(0x2C);

	LCD_WriteCommand(LCD_PWCTR2);       // VDV 和 VRH 来源设置
	LCD_WriteData_8bit(0x01);     // VDV 和 VRH 由用户自由配置

	LCD_WriteCommand(LCD_PWCTR3);		// VRH电压 设置指令
	LCD_WriteData_8bit(0x12);     // VRH电压 = 4.6+( vcom+vcom offset+vdv)

	LCD_WriteCommand(LCD_PWCTR4);		   // VDV电压 设置指令
	LCD_WriteData_8bit(0x20);     // VDV电压 = 0v

	LCD_WriteCommand(LCD_PWCTR5); 		// 正常模式的帧率控制指令
	LCD_WriteData_8bit(0x0F);     	// 设置屏幕控制器的刷新帧率为60帧

	LCD_WriteCommand(LCD_VMCTR1);		// 电源控制指令
	LCD_WriteData_8bit(0xA4);     // 无效数据，固定写入0xA4
	LCD_WriteData_8bit(0xA1);     // AVDD = 6.8V ，AVDD = -4.8V ，VDS = 2.3V

	LCD_WriteCommand(LCD_GMCTRP1);       // 正极电压伽马值设定
	LCD_WriteData_8bit(0xD0);
	LCD_WriteData_8bit(0x04);
	LCD_WriteData_8bit(0x0D);
	LCD_WriteData_8bit(0x11);
	LCD_WriteData_8bit(0x13);
	LCD_WriteData_8bit(0x2B);
	LCD_WriteData_8bit(0x3F);
	LCD_WriteData_8bit(0x54);
	LCD_WriteData_8bit(0x4C);
	LCD_WriteData_8bit(0x18);
	LCD_WriteData_8bit(0x0D);
	LCD_WriteData_8bit(0x0B);
	LCD_WriteData_8bit(0x1F);
	LCD_WriteData_8bit(0x23);

	LCD_WriteCommand(LCD_GMCTRN1);      // 负极电压伽马值设定
	LCD_WriteData_8bit(0xD0);
	LCD_WriteData_8bit(0x04);
	LCD_WriteData_8bit(0x0C);
	LCD_WriteData_8bit(0x11);
	LCD_WriteData_8bit(0x13);
	LCD_WriteData_8bit(0x2C);
	LCD_WriteData_8bit(0x3F);
	LCD_WriteData_8bit(0x44);
	LCD_WriteData_8bit(0x51);
	LCD_WriteData_8bit(0x2F);
	LCD_WriteData_8bit(0x1F);
	LCD_WriteData_8bit(0x1F);
	LCD_WriteData_8bit(0x20);
	LCD_WriteData_8bit(0x23);

	LCD_WriteCommand(LCD_INVERSION_ON);       // 打开反显，因为面板是常黑型，操作需要反过来

	// 退出休眠指令，LCD控制器在刚上电、复位时，会自动进入休眠模式 ，因此操作屏幕之前，需要退出休眠
	LCD_WriteCommand(LCD_SLEEP_OUT);       // 退出休眠 指令
	HAL_Delay(120);               // 需要等待120ms，让电源电压和时钟电路稳定下来

	// 打开显示指令，LCD控制器在刚上电、复位时，会自动关闭显示
	LCD_WriteCommand(LCD_DISPLAY_ON);       // 打开显示

	// 以下进行一些驱动的默认设置
	LCD_SetDirection(Direction_V);   	      // 设置显示方向
	LCD_SetBackColor(LCD_BLACK);           // 设置背景色
	LCD_SetColor(LCD_WHITE);               // 设置画笔色

	MX_TIM23_Init();	// 初始化背光PWM
	LCD_Backlight_ON;
}

/**
 * @brief 设置显示区域函数
 *
 * @param x1 起始X坐标
 * @param y1 起始Y坐标
 * @param x2 结束X坐标
 * @param y2 结束Y坐标
 */
void LCD_SetAddress(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2) {
	LCD_WriteCommand(LCD_CASET);		//	列地址设置，即X坐标
	LCD_WriteData_16bit(x1+hlcd.X_Offset);
	LCD_WriteData_16bit(x2+hlcd.X_Offset);

	LCD_WriteCommand(LCD_RASET);		//	行地址设置，即Y坐标
	LCD_WriteData_16bit(y1+hlcd.Y_Offset);
	LCD_WriteData_16bit(y2+hlcd.Y_Offset);

	LCD_WriteCommand(LCD_RAMWR);		//	开始写入显存，即要显示的颜色数据
}

/**
 * @brief LCD初始化函数
 */
void LCD_Init(void) {
	hlcd.init.hspi = &hspi6;
	hlcd.init.hdma2d = &hdma2d;
	// hlcd.init.hmdma = &hmdma_mdma_channel0_sw_0;
	hlcd.init.width = LCD_Width;
	hlcd.init.height = LCD_Height;

	SPI_LCD_Init();
}

/**
 * @brief 设置画笔颜色函数
 *
 * @param Color 要设置的RGB565颜色值
 */
void LCD_SetColor(uint32_t Color) {
	uint16_t Red_Value = 0, Green_Value = 0, Blue_Value = 0; //各个颜色通道的值

	Red_Value   = (uint16_t)((Color&0x00F80000)>>8);   // 转换成 16位 的RGB565颜色
	Green_Value = (uint16_t)((Color&0x0000FC00)>>5);
	Blue_Value  = (uint16_t)((Color&0x000000F8)>>3);

	hlcd.Color = (uint16_t)(Red_Value | Green_Value | Blue_Value);  // 将颜色写入全局LCD参数
}

/**
 * @brief 设置背景颜色函数
 *
 * @param Color 要设置的背景RGB565颜色值
 */
void LCD_SetBackColor(uint32_t Color) {
	hlcd.BackColor = Color;	// 将颜色写入全局LCD参数
}

/**
 * @brief 设置显示方向函数
 *
 * @param direction 要设置的显示方向，可选值为Direction_H、Direction_V、Direction_H_Flip、Direction_V_Flip
 */
void LCD_SetDirection(uint8_t direction) {
	hlcd.Direction = direction;    // 写入全局LCD参数

   switch (direction) {
   case Direction_H:           // 横屏显示
      LCD_WriteCommand(LCD_MADCTL);      // 显存访问控制 指令，用于设置访问显存的方式
      LCD_WriteData_8bit(0x70);          // 横屏显示
      hlcd.X_Offset   = 0;               // 设置控制器坐标偏移量
      hlcd.Y_Offset   = 0;
      hlcd.init.width  = LCD_Height;     // 重新赋值长、宽
      hlcd.init.height = LCD_Width;
      break;
   case Direction_V:           // 垂直显示
      LCD_WriteCommand(LCD_MADCTL);      // 显存访问控制 指令，用于设置访问显存的方式
      LCD_WriteData_8bit(0x00);          // 垂直显示
      hlcd.X_Offset   = 0;               // 设置控制器坐标偏移量
      hlcd.Y_Offset   = 0;
      hlcd.init.width  = LCD_Width;      // 重新赋值长、宽
      hlcd.init.height = LCD_Height;
      break;
   case Direction_H_Flip:    // 横屏显示并上下翻转
      LCD_WriteCommand(LCD_MADCTL);      // 显存访问控制 指令，用于设置访问显存的方式
      LCD_WriteData_8bit(0xA0);          // 横屏显示，并上下翻转，RGB像素格式
      hlcd.X_Offset   = 80;              // 设置控制器坐标偏移量
      hlcd.Y_Offset   = 0;
      hlcd.init.width  = LCD_Height;     // 重新赋值长、宽
      hlcd.init.height = LCD_Width;
      break;
   case Direction_V_Flip:    // 垂直显示并上下翻转
      LCD_WriteCommand(LCD_MADCTL);      // 显存访问控制 指令，用于设置访问显存的方式
      LCD_WriteData_8bit(0xC0);          // 垂直显示，并上下翻转，RGB像素格式
      hlcd.X_Offset   = 0;               // 设置控制器坐标偏移量
      hlcd.Y_Offset   = 80;
      hlcd.init.width  = LCD_Width;      // 重新赋值长、宽
      hlcd.init.height = LCD_Height;
      break;
   default: break;
   }
}
