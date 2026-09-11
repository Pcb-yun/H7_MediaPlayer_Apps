#include "boot.h"
#include "boot_shared.h"
#include "stm32h7xx_hal.h"
#include <string.h>

static BootSharedMem_t bootmem __attribute__((at(BOOT_SHARED_ADD)));
const AppHand_t apphand __attribute__((at(APP_HAND_ADDRESS))) = {
    .magic = BOOT_SHARED_MAGIC,
    .build_date = __DATE__,
    .build_time = __TIME__
};

/**
 * @brief 初始化共享内存结构体
 */
void BootShared_Init(void) {
    __HAL_RCC_BKPRAM_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    bootmem.magic = BOOT_SHARED_MAGIC;
    bootmem.bootmode = BOOT_MODE_NORMAL;
}

/**
 * @brief 请求系统更新
 * @param path 8.3 格式的固件文件名
 */
void BootShared_Update(const uint8_t *path) {
    uint8_t tmp[16] = {0};

    size_t len = strlen((const char *)path);
    if (len > 15)
        len = 15;
    memcpy(tmp, path, len);

    // 由于ECC单字缓存，强制使用32位对齐写入
    uint32_t *dst = (uint32_t *)bootmem.filePath;
    uint32_t *src = (uint32_t *)tmp;
    for (int i = 0; i < 4; i++)
    {
        dst[i] = src[i];
    }

    bootmem.bootmode = BOOT_MODE_LOCAL;
    bootmem.magic = BOOT_SHARED_MAGIC;

    __DSB();
    NVIC_SystemReset();
}
