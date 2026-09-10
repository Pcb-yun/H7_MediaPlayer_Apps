/**
 * Copyright (c) 2015 - present LibDriver All rights reserved
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * @file      driver_w25qxx_interface.c
 * @brief     driver w25qxx interface source file
 * @version   1.0.0
 * @author    Shifeng Li
 * @date      2021-07-15
 *
 * <h3>history</h3>
 * <table>
 * <tr><th>Date        <th>Version  <th>Author      <th>Description
 * <tr><td>2021/07/15  <td>1.0      <td>Shifeng Li  <td>first upload
 * </table>
 */

#include "driver_w25qxx_interface.h"
#include "octospi.h"
#include "cmsis_os2.h"
#include "log.h"
#include <stdarg.h>
#include <stdio.h>


/**
 * @brief  interface spi qspi bus init
 * @return status code
 *         - 0 success
 *         - 1 spi qspi init failed
 * @note   none
 */
uint8_t w25qxx_interface_spi_qspi_init(void)
{
    MX_OCTOSPI1_Init();

    return 0;
}

/**
 * @brief  interface spi qspi bus deinit
 * @return status code
 *         - 0 success
 *         - 1 spi qspi deinit failed
 * @note   none
 */
uint8_t w25qxx_interface_spi_qspi_deinit(void)
{
    HAL_OSPI_MspDeInit(&hospi1);

    return 0;
}

/**
 * @brief      interface spi qspi bus write read
 * @param[in]  instruction sent instruction
 * @param[in]  instruction_line instruction phy lines
 * @param[in]  address register address
 * @param[in]  address_line address phy lines
 * @param[in]  address_len address length
 * @param[in]  alternate register address
 * @param[in]  alternate_line alternate phy lines
 * @param[in]  alternate_len alternate length
 * @param[in]  dummy dummy cycle
 * @param[in]  *in_buf pointer to a input buffer
 * @param[in]  in_len input length
 * @param[out] *out_buf pointer to a output buffer
 * @param[in]  out_len output length
 * @param[in]  data_line data phy lines
 * @return     status code
 *             - 0 success
 *             - 1 write read failed
 * @note       none
 */
uint8_t w25qxx_interface_spi_qspi_write_read(uint8_t instruction, uint8_t instruction_line,
                                             uint32_t address, uint8_t address_line, uint8_t address_len,
                                             uint32_t alternate, uint8_t alternate_line, uint8_t alternate_len,
                                             uint8_t dummy, uint8_t *in_buf, uint32_t in_len,
                                             uint8_t *out_buf, uint32_t out_len, uint8_t data_line)
{
    // 阶段线宽映射表, 索引为线宽(0/1/2/4), 3 线为非法项
    static const uint32_t s_instruction_mode[] = {
        HAL_OSPI_INSTRUCTION_NONE,      // 无线宽: 无指令阶段
        HAL_OSPI_INSTRUCTION_1_LINE,    // 1 线
        HAL_OSPI_INSTRUCTION_2_LINES,   // 2 线
        0xFFFFFFFF,                     // 3 线非法
        HAL_OSPI_INSTRUCTION_4_LINES    // 4 线
    };
    static const uint32_t s_address_mode[] = {
        HAL_OSPI_ADDRESS_NONE,          // 无线宽: 无地址阶段
        HAL_OSPI_ADDRESS_1_LINE,        // 1 线
        HAL_OSPI_ADDRESS_2_LINES,       // 2 线
        0xFFFFFFFF,                     // 3 线非法
        HAL_OSPI_ADDRESS_4_LINES        // 4 线
    };
    static const uint32_t s_alternate_mode[] = {
        HAL_OSPI_ALTERNATE_BYTES_NONE,      // 无线宽: 无交替字节阶段
        HAL_OSPI_ALTERNATE_BYTES_1_LINE,    // 1 线
        HAL_OSPI_ALTERNATE_BYTES_2_LINES,   // 2 线
        0xFFFFFFFF,                         // 3 线非法
        HAL_OSPI_ALTERNATE_BYTES_4_LINES    // 4 线
    };
    static const uint32_t s_data_mode[] = {
        HAL_OSPI_DATA_NONE,         // 无线宽: 无数据阶段
        HAL_OSPI_DATA_1_LINE,       // 1 线
        HAL_OSPI_DATA_2_LINES,      // 2 线
        0xFFFFFFFF,                 // 3 线非法
        HAL_OSPI_DATA_4_LINES       // 4 线
    };
    // 长度(字节)转位宽配置映射表, 索引为字节数(0~4)
    static const uint32_t s_address_size[] = {
        HAL_OSPI_ADDRESS_8_BITS, HAL_OSPI_ADDRESS_8_BITS,
        HAL_OSPI_ADDRESS_16_BITS, HAL_OSPI_ADDRESS_24_BITS, HAL_OSPI_ADDRESS_32_BITS
    };
    static const uint32_t s_alternate_size[] = {
        HAL_OSPI_ALTERNATE_BYTES_8_BITS, HAL_OSPI_ALTERNATE_BYTES_8_BITS,
        HAL_OSPI_ALTERNATE_BYTES_16_BITS, HAL_OSPI_ALTERNATE_BYTES_24_BITS, HAL_OSPI_ALTERNATE_BYTES_32_BITS
    };
    OSPI_RegularCmdTypeDef cmd = {0};
    HAL_StatusTypeDef status;
    uint32_t data_len;

    // 校验各阶段线宽, 只支持 0/1/2/4 线
    if ((instruction_line > 4) || (s_instruction_mode[instruction_line] == 0xFFFFFFFF) ||
        (address_line > 4) || (s_address_mode[address_line] == 0xFFFFFFFF) ||
        (alternate_line > 4) || (s_alternate_mode[alternate_line] == 0xFFFFFFFF) ||
        (data_line > 4) || (s_data_mode[data_line] == 0xFFFFFFFF) ||
        (address_len > 4) || (alternate_len > 4))
    {
        return 1;
    }
    // OCTOSPI 单帧只能包含一个数据方向, 写读不能同时存在
    if ((in_len != 0) && (out_len != 0))
    {
        return 1;
    }

    // 数据阶段长度: 优先读(接收), 否则为写(发送), 均为 0 则无数据阶段
    data_len = (out_len != 0) ? out_len : in_len;

    cmd.OperationType = HAL_OSPI_OPTYPE_COMMON_CFG;         // 普通间接模式配置
    cmd.FlashId = HAL_OSPI_FLASH_ID_1;                      // 选择外部 Flash 1
    cmd.Instruction = instruction;                          // 指令码
    cmd.InstructionMode = s_instruction_mode[instruction_line]; // 指令阶段线宽
    cmd.InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS;      // 指令为 1 字节
    cmd.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
    cmd.Address = address;                                  // 地址值
    cmd.AddressMode = s_address_mode[address_line];         // 地址阶段线宽
    cmd.AddressSize = s_address_size[address_len];          // 地址位宽
    cmd.AddressDtrMode = HAL_OSPI_ADDRESS_DTR_DISABLE;
    cmd.AlternateBytes = alternate;                         // 交替字节
    cmd.AlternateBytesMode = s_alternate_mode[alternate_line]; // 交替字节阶段线宽
    cmd.AlternateBytesSize = s_alternate_size[alternate_len]; // 交替字节位宽
    cmd.AlternateBytesDtrMode = HAL_OSPI_ALTERNATE_BYTES_DTR_DISABLE;
    cmd.DataMode = (data_len != 0) ? s_data_mode[data_line] : HAL_OSPI_DATA_NONE; // 数据阶段线宽
    cmd.NbData = data_len;                                  // 数据长度
    cmd.DataDtrMode = HAL_OSPI_DATA_DTR_DISABLE;
    cmd.DummyCycles = dummy;                                // 假时钟周期数
    cmd.DQSMode = HAL_OSPI_DQS_DISABLE;
    cmd.SIOOMode = HAL_OSPI_SIOO_INST_EVERY_CMD;            // 每次命令都发指令

    // 下发命令配置(含地址/交替字节/假周期, 无数据阶段时命令执行完成后即结束)
    status = HAL_OSPI_Command(&hospi1, &cmd, HAL_OSPI_TIMEOUT_DEFAULT_VALUE);
    if (status != HAL_OK)
    {
        return 1;
    }
    if (out_len != 0)
    {
        // 读数据阶段
        status = HAL_OSPI_Receive(&hospi1, out_buf, HAL_OSPI_TIMEOUT_DEFAULT_VALUE);
    }
    else if (in_len != 0)
    {
        // 写数据阶段
        status = HAL_OSPI_Transmit(&hospi1, in_buf, HAL_OSPI_TIMEOUT_DEFAULT_VALUE);
    }
    if (status != HAL_OK)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief     interface delay ms
 * @param[in] ms time
 * @note      none
 */
void w25qxx_interface_delay_ms(uint32_t ms)
{
    osDelay(ms);
}

/**
 * @brief     interface delay us
 * @param[in] us time
 * @note      none
 */
void w25qxx_interface_delay_us(uint32_t us)
{

}

/**
 * @brief     interface print format data
 * @param[in] fmt format data
 * @note      none
 */
void w25qxx_interface_debug_print(const char *const fmt, ...)
{
    uint8_t buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf((char *)buffer, sizeof(buffer), fmt, args);
    va_end(args);

    logPrintln("%s", buffer);
}
