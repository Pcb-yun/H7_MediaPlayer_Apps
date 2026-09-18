#ifndef LVGL_FATFS_H
#define LVGL_FATFS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LVGL_FATFS_LETTER 'C'       // LVGL中SD卡文件系统使用的盘符

bool lvgl_fatfs_init(void);





#ifdef __cplusplus
}
#endif

#endif
