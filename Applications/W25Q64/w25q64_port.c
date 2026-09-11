/**
 * @file w25q64_port.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief W25Q64 接口层源文件
 */

#include "driver_w25qxx_interface.h"
#include "cmsis_os2.h"
#include "octospi.h"
#include "w25q64_port.h"

#define W25Q64_CMD_WRITE_STATUS_REG2          0x31U
#define W25Q64_CMD_READ_STATUS_REG1           0x05U
#define W25Q64_CMD_READ_STATUS_REG2           0x35U
#define W25Q64_CMD_VOLATILE_SR_WRITE_ENABLE   0x50U
#define W25Q64_CMD_QUAD_PAGE_PROGRAM          0x32U
#define W25Q64_CMD_FAST_READ_QUAD_OUTPUT      0x6BU
#define W25Q64_CMD_JEDEC_ID                   0x9FU
#define W25Q64_CMD_ENABLE_RESET               0x66U
#define W25Q64_CMD_RESET_DEVICE               0x99U

#define W25Q64_STATUS1_BUSY                   0x01U
#define W25Q64_STATUS2_QE                     0x02U
#define W25Q64_READY_TIMEOUT_MS               100U

#define W25Q64_JEDEC_MANUFACTURER             0xEFU
#define W25Q64_JEDEC_MEMORY_TYPE              0x40U
#define W25Q64_JEDEC_CAPACITY                 0x17U

#if !W25Q64_ONLY_MEMORY_MAPPED
static w25qxx_handle_t gs_handle;
#else
/**
 * @brief 填充 OCTOSPI 命令的通用配置
 * @param command OCTOSPI 命令结构体指针
 * @param instruction W25Q64 指令码
 * @param instruction_mode 指令阶段的数据线宽度
 * @return None
 */
static void W25Q64_PrepareCommand(OSPI_RegularCmdTypeDef *command, uint8_t instruction,
                                  uint32_t instruction_mode) {
    *command = (OSPI_RegularCmdTypeDef){0};
    command->OperationType = HAL_OSPI_OPTYPE_COMMON_CFG;
    command->FlashId = HAL_OSPI_FLASH_ID_1;
    command->Instruction = instruction;
    command->InstructionMode = instruction_mode;
    command->InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS;
    command->InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
    command->AddressMode = HAL_OSPI_ADDRESS_NONE;
    command->AddressDtrMode = HAL_OSPI_ADDRESS_DTR_DISABLE;
    command->AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    command->AlternateBytesDtrMode = HAL_OSPI_ALTERNATE_BYTES_DTR_DISABLE;
    command->DataMode = HAL_OSPI_DATA_NONE;
    command->DataDtrMode = HAL_OSPI_DATA_DTR_DISABLE;
    command->DQSMode = HAL_OSPI_DQS_DISABLE;
    command->SIOOMode = HAL_OSPI_SIOO_INST_EVERY_CMD;
}

/**
 * @brief 发送不包含地址和数据阶段的 W25Q64 指令
 * @param instruction W25Q64 指令码
 * @param instruction_mode 指令阶段的数据线宽度
 * @return true 发送成功, false 发送失败
 */
static bool W25Q64_SendInstruction(uint8_t instruction, uint32_t instruction_mode) {
    OSPI_RegularCmdTypeDef command;

    W25Q64_PrepareCommand(&command, instruction, instruction_mode);
    return HAL_OSPI_Command(&hospi1, &command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) == HAL_OK;
}

/**
 * @brief 通过单线 SPI 读取 W25Q64 寄存器
 * @param instruction 寄存器读取指令
 * @param data 读取数据的存储缓冲区
 * @param size 读取字节数
 * @return true 读取成功, false 读取失败
 */
static bool W25Q64_ReadRegister(uint8_t instruction, uint8_t *data, uint32_t size) {
    OSPI_RegularCmdTypeDef command;

    if ((data == NULL) || (size == 0U)) {
        return false;
    }

    W25Q64_PrepareCommand(&command, instruction, HAL_OSPI_INSTRUCTION_1_LINE);
    command.DataMode = HAL_OSPI_DATA_1_LINE;
    command.NbData = size;

    if (HAL_OSPI_Command(&hospi1, &command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return false;
    }

    return HAL_OSPI_Receive(&hospi1, data, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) == HAL_OK;
}

/**
 * @brief 通过单线 SPI 写入 W25Q64 寄存器
 * @param instruction 寄存器写入指令
 * @param data 待写入数据缓冲区
 * @param size 写入字节数
 * @return true 写入成功, false 写入失败
 */
static bool W25Q64_WriteRegister(uint8_t instruction, const uint8_t *data, uint32_t size) {
    OSPI_RegularCmdTypeDef command;

    if ((data == NULL) || (size == 0U)) {
        return false;
    }

    W25Q64_PrepareCommand(&command, instruction, HAL_OSPI_INSTRUCTION_1_LINE);
    command.DataMode = HAL_OSPI_DATA_1_LINE;
    command.NbData = size;

    if (HAL_OSPI_Command(&hospi1, &command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return false;
    }

    return HAL_OSPI_Transmit(&hospi1, (uint8_t *)data, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) == HAL_OK;
}

/**
 * @brief 复位 W25Q64 并统一恢复为单线 SPI 模式
 * @return true 复位成功, false 复位失败
 */
static bool W25Q64_ResetToSpiMode(void) {
    /* 如果 Flash 由上一次运行留在 QPI 模式，先用四线复位。 */
    if (!W25Q64_SendInstruction(W25Q64_CMD_ENABLE_RESET, HAL_OSPI_INSTRUCTION_4_LINES) ||
        !W25Q64_SendInstruction(W25Q64_CMD_RESET_DEVICE, HAL_OSPI_INSTRUCTION_4_LINES)) {
        return false;
    }
    (void)osDelay(1U);

    /* 再用 SPI 指令复位，保证后续命令始终以 1-1-x 模式发送。 */
    if (!W25Q64_SendInstruction(W25Q64_CMD_ENABLE_RESET, HAL_OSPI_INSTRUCTION_1_LINE) ||
        !W25Q64_SendInstruction(W25Q64_CMD_RESET_DEVICE, HAL_OSPI_INSTRUCTION_1_LINE)) {
        return false;
    }
    (void)osDelay(1U);

    return true;
}

/**
 * @brief 等待 W25Q64 完成内部操作
 * @param timeout_ms 最大等待时间, 单位为毫秒
 * @return true 芯片就绪, false 通信失败或等待超时
 */
static bool W25Q64_WaitReady(uint32_t timeout_ms) {
    uint8_t status;

    while (timeout_ms > 0U) {
        if (!W25Q64_ReadRegister(W25Q64_CMD_READ_STATUS_REG1, &status, 1U)) {
            return false;
        }
        if ((status & W25Q64_STATUS1_BUSY) == 0U) {
            return true;
        }
        (void)osDelay(1U);
        timeout_ms--;
    }

    return false;
}

/**
 * @brief 检查并开启 W25Q64 四线数据模式
 * @return true QE 位已置位, false 配置失败
 */
static bool W25Q64_EnableQuadMode(void) {
    uint8_t status2;

    if (!W25Q64_ReadRegister(W25Q64_CMD_READ_STATUS_REG2, &status2, 1U)) {
        return false;
    }
    if ((status2 & W25Q64_STATUS2_QE) != 0U) {
        return true;
    }

    /* 使用挥发性状态寄存器写使能，避免为设置 QE 频繁擦写非挥发位。 */
    if (!W25Q64_SendInstruction(W25Q64_CMD_VOLATILE_SR_WRITE_ENABLE,
                                HAL_OSPI_INSTRUCTION_1_LINE)) {
        return false;
    }

    status2 |= W25Q64_STATUS2_QE;
    if (!W25Q64_WriteRegister(W25Q64_CMD_WRITE_STATUS_REG2, &status2, 1U) ||
        !W25Q64_WaitReady(W25Q64_READY_TIMEOUT_MS) ||
        !W25Q64_ReadRegister(W25Q64_CMD_READ_STATUS_REG2, &status2, 1U)) {
        return false;
    }

    return (status2 & W25Q64_STATUS2_QE) != 0U;
}

/**
 * @brief 配置 OCTOSPI 的 W25Q64 内存映射读写命令
 * @return true 已进入内存映射模式, false 配置失败
 */
static bool W25Q64_ConfigureMemoryMapped(void) {
    OSPI_RegularCmdTypeDef command;
    OSPI_MemoryMappedTypeDef memory_mapped = {
        .TimeOutActivation = HAL_OSPI_TIMEOUT_COUNTER_DISABLE,
        .TimeOutPeriod = 0U,
    };

    W25Q64_PrepareCommand(&command, W25Q64_CMD_FAST_READ_QUAD_OUTPUT,
                          HAL_OSPI_INSTRUCTION_1_LINE);
    command.OperationType = HAL_OSPI_OPTYPE_READ_CFG;
    command.AddressMode = HAL_OSPI_ADDRESS_1_LINE;
    command.AddressSize = HAL_OSPI_ADDRESS_24_BITS;
    command.DataMode = HAL_OSPI_DATA_4_LINES;
    command.DummyCycles = 8U;
    command.NbData = 1U;
    if (HAL_OSPI_Command(&hospi1, &command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return false;
    }

    /* HAL 要求同时存在读、写配置才能进入 memory-mapped 状态。
       应用层不通过 0x90000000 做 AHB 写入，该配置仅用于完成 HAL 状态机。 */
    W25Q64_PrepareCommand(&command, W25Q64_CMD_QUAD_PAGE_PROGRAM,
                          HAL_OSPI_INSTRUCTION_1_LINE);
    command.OperationType = HAL_OSPI_OPTYPE_WRITE_CFG;
    command.AddressMode = HAL_OSPI_ADDRESS_1_LINE;
    command.AddressSize = HAL_OSPI_ADDRESS_24_BITS;
    command.DataMode = HAL_OSPI_DATA_4_LINES;
    command.NbData = 1U;
    if (HAL_OSPI_Command(&hospi1, &command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return false;
    }

    return HAL_OSPI_MemoryMapped(&hospi1, &memory_mapped) == HAL_OK;
}
#endif

/**
 * @brief 初始化 W25Q64 并进入 OCTOSPI 内存映射模式
 * @return true 初始化成功, false 初始化失败
 */
bool W25Q64_Init(void) {
#if W25Q64_ONLY_MEMORY_MAPPED
    uint8_t jedec_id[3];

    if (HAL_OSPI_IsMemoryMapped(&hospi1) != 0U) {
        return true;
    }

    MX_OCTOSPI1_Init();
    if (!W25Q64_ResetToSpiMode() ||
        !W25Q64_ReadRegister(W25Q64_CMD_JEDEC_ID, jedec_id, sizeof(jedec_id))) {
        return false;
    }

    if ((jedec_id[0] != W25Q64_JEDEC_MANUFACTURER) ||
        (jedec_id[1] != W25Q64_JEDEC_MEMORY_TYPE) ||
        (jedec_id[2] != W25Q64_JEDEC_CAPACITY)) {
        return false;
    }

    return W25Q64_EnableQuadMode() && W25Q64_ConfigureMemoryMapped();
#else
    uint8_t res;

    DRIVER_W25QXX_LINK_INIT(&gs_handle, w25qxx_handle_t);
    DRIVER_W25QXX_LINK_SPI_QSPI_INIT(&gs_handle, w25qxx_interface_spi_qspi_init);
    DRIVER_W25QXX_LINK_SPI_QSPI_DEINIT(&gs_handle, w25qxx_interface_spi_qspi_deinit);
    DRIVER_W25QXX_LINK_SPI_QSPI_WRITE_READ(&gs_handle, w25qxx_interface_spi_qspi_write_read);
    DRIVER_W25QXX_LINK_DELAY_MS(&gs_handle, w25qxx_interface_delay_ms);
    DRIVER_W25QXX_LINK_DELAY_US(&gs_handle, w25qxx_interface_delay_us);
    DRIVER_W25QXX_LINK_DEBUG_PRINT(&gs_handle, w25qxx_interface_debug_print);

    /* set chip type */
    res = w25qxx_set_type(&gs_handle, W25Q64);
    if (res != 0) {
        w25qxx_interface_debug_print("w25qxx: set type failed.\n");

        return false;
    }

    /* set chip interface */
    res = w25qxx_set_interface(&gs_handle, W25QXX_INTERFACE_QSPI);
    if (res != 0) {
        w25qxx_interface_debug_print("w25qxx: set interface failed.\n");

        return false;
    }

    /* set dual quad spi */
    res = w25qxx_set_dual_quad_spi(&gs_handle, W25QXX_BOOL_TRUE);
    if (res != 0) {
        w25qxx_interface_debug_print("w25qxx: set dual quad spi failed.\n");
        (void)w25qxx_deinit(&gs_handle);

        return false;
    }

    /* chip init */
    res = w25qxx_init(&gs_handle);
    if (res != 0) {
        w25qxx_interface_debug_print("w25qxx: init failed.\n");

        return false;
    }

    return true;
#endif
}
