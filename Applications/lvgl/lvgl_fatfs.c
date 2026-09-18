/**
 * @file lvgl_fatfs.c
 * @brief LVGL的FatFs文件系统驱动
 */

#include "lvgl_fatfs.h"

#include "fatfs.h"
#include "lvgl.h"

#include <stdint.h>

#define LVGL_FATFS_CACHE_SIZE 1024U
#define LVGL_FATFS_PATH_SIZE  256U

static lv_fs_drv_t lvgl_fatfs_drv __attribute__((section(".DTCM")));

/**
 * @brief 将FatFs的返回码转换为LVGL文件系统返回码
 * @param result FatFs返回码
 * @return lv_fs_res_t 对应的LVGL文件系统返回码
 */
static lv_fs_res_t fatfs_result_to_lvgl(FRESULT result)
{
    switch(result) {
        case FR_OK:
            return LV_FS_RES_OK;
        case FR_DISK_ERR:
        case FR_NOT_READY:
            return LV_FS_RES_HW_ERR;
        case FR_INT_ERR:
        case FR_INVALID_OBJECT:
        case FR_NOT_ENABLED:
        case FR_NO_FILESYSTEM:
            return LV_FS_RES_FS_ERR;
        case FR_NO_FILE:
        case FR_NO_PATH:
        case FR_INVALID_DRIVE:
            return LV_FS_RES_NOT_EX;
        case FR_DENIED:
        case FR_EXIST:
        case FR_WRITE_PROTECTED:
            return LV_FS_RES_DENIED;
        case FR_TIMEOUT:
            return LV_FS_RES_TOUT;
        case FR_LOCKED:
        case FR_TOO_MANY_OPEN_FILES:
            return LV_FS_RES_LOCKED;
        case FR_NOT_ENOUGH_CORE:
            return LV_FS_RES_OUT_OF_MEM;
        case FR_INVALID_NAME:
        case FR_INVALID_PARAMETER:
            return LV_FS_RES_INV_PARAM;
        default:
            return LV_FS_RES_UNKNOWN;
    }
}

/**
 * @brief 将LVGL传入的UTF-8路径转换为FatFs使用的UTF-16路径
 * @param path LVGL传入的UTF-8相对路径
 * @param path_utf16 输出的UTF-16路径缓冲区
 * @param path_size 输出缓冲区大小
 * @note 会自动在路径前拼接SD卡盘符SDPath
 */
static void path_to_utf16(const char *path, TCHAR *path_utf16, size_t path_size)
{
    size_t prefix_length;

    if(path_size == 0U) return;

    prefix_length = utf8to16((const uint8_t *)SDPath, path_utf16, path_size);
    if(prefix_length < path_size - 1U) {
        (void)utf8to16((const uint8_t *)path, &path_utf16[prefix_length],
                       path_size - prefix_length);
    }
}

/**
 * @brief 查询文件系统是否就绪
 * @param drv LVGL文件系统驱动指针
 * @return bool true表示SD卡文件系统可用
 */
static bool fs_ready(lv_fs_drv_t *drv)
{
    LV_UNUSED(drv);
    return FS_Check();
}

/**
 * @brief 打开文件回调函数
 * @param drv LVGL文件系统驱动指针
 * @param path 文件路径
 * @param mode 打开模式
 * @return void* 成功返回FIL对象指针，失败返回NULL
 * @note 写模式带FA_OPEN_ALWAYS，文件不存在时自动创建
 */
static void *fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    BYTE flags = 0U;
    FIL *file;
    TCHAR path_utf16[LVGL_FATFS_PATH_SIZE];

    LV_UNUSED(drv);

    if((mode & LV_FS_MODE_RD) != 0U) flags |= FA_READ;
    if((mode & LV_FS_MODE_WR) != 0U) flags |= FA_WRITE | FA_OPEN_ALWAYS;
    if(flags == 0U) return NULL;

    file = lv_malloc(sizeof(FIL));
    if(file == NULL) return NULL;

    path_to_utf16(path, path_utf16, LVGL_FATFS_PATH_SIZE);
    if(f_open(file, path_utf16, flags) != FR_OK) {
        lv_free(file);
        return NULL;
    }

    return file;
}

/**
 * @brief 关闭文件回调函数
 * @param drv LVGL文件系统驱动指针
 * @param file_p FIL对象指针
 * @return lv_fs_res_t 关闭结果
 */
static lv_fs_res_t fs_close(lv_fs_drv_t *drv, void *file_p)
{
    FRESULT result;

    LV_UNUSED(drv);
    result = f_close((FIL *)file_p);
    lv_free(file_p);
    return fatfs_result_to_lvgl(result);
}

/**
 * @brief 读取文件回调函数
 * @param drv LVGL文件系统驱动指针
 * @param file_p FIL对象指针
 * @param buf 接收数据缓冲区
 * @param bytes_to_read 期望读取的字节数
 * @param bytes_read 实际读取的字节数
 * @return lv_fs_res_t 读取结果
 */
static lv_fs_res_t fs_read(lv_fs_drv_t *drv, void *file_p, void *buf,
                           uint32_t bytes_to_read, uint32_t *bytes_read)
{
    UINT read_count = 0U;
    FRESULT result;

    LV_UNUSED(drv);
    result = f_read((FIL *)file_p, buf, (UINT)bytes_to_read, &read_count);
    if(bytes_read != NULL) *bytes_read = read_count;
    return fatfs_result_to_lvgl(result);
}

/**
 * @brief 写入文件回调函数
 * @param drv LVGL文件系统驱动指针
 * @param file_p FIL对象指针
 * @param buf 待写入数据缓冲区
 * @param bytes_to_write 期望写入的字节数
 * @param bytes_written 实际写入的字节数
 * @return lv_fs_res_t 写入结果
 */
static lv_fs_res_t fs_write(lv_fs_drv_t *drv, void *file_p, const void *buf,
                            uint32_t bytes_to_write, uint32_t *bytes_written)
{
    UINT write_count = 0U;
    FRESULT result;

    LV_UNUSED(drv);
    result = f_write((FIL *)file_p, buf, (UINT)bytes_to_write, &write_count);
    if(bytes_written != NULL) *bytes_written = write_count;
    return fatfs_result_to_lvgl(result);
}

/**
 * @brief 文件定位回调函数
 * @param drv LVGL文件系统驱动指针
 * @param file_p FIL对象指针
 * @param position 相对定位基准的偏移量
 * @param whence 定位基准
 * @return lv_fs_res_t 定位结果
 */
static lv_fs_res_t fs_seek(lv_fs_drv_t *drv, void *file_p, uint32_t position,
                           lv_fs_whence_t whence)
{
    FIL *file = (FIL *)file_p;
    FSIZE_t target;

    LV_UNUSED(drv);

    switch(whence) {
        case LV_FS_SEEK_SET:
            target = position;
            break;
        case LV_FS_SEEK_CUR:
            target = f_tell(file) + position;
            break;
        case LV_FS_SEEK_END:
            target = f_size(file) + position;
            break;
        default:
            return LV_FS_RES_INV_PARAM;
    }

    return fatfs_result_to_lvgl(f_lseek(file, target));
}

/**
 * @brief 获取文件当前位置回调函数
 * @param drv LVGL文件系统驱动指针
 * @param file_p FIL对象指针
 * @param position 输出的当前偏移量
 * @return lv_fs_res_t 获取结果
 */
static lv_fs_res_t fs_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *position)
{
    LV_UNUSED(drv);
    if(position == NULL) return LV_FS_RES_INV_PARAM;
    *position = (uint32_t)f_tell((FIL *)file_p);
    return LV_FS_RES_OK;
}

/**
 * @brief 打开目录回调函数
 * @param drv LVGL文件系统驱动指针
 * @param path 目录路径
 * @return void* 成功返回DIR对象指针，失败返回NULL
 */
static void *fs_dir_open(lv_fs_drv_t *drv, const char *path)
{
    DIR *dir;
    TCHAR path_utf16[LVGL_FATFS_PATH_SIZE];

    LV_UNUSED(drv);
    dir = lv_malloc(sizeof(DIR));
    if(dir == NULL) return NULL;

    path_to_utf16(path, path_utf16, LVGL_FATFS_PATH_SIZE);
    if(f_opendir(dir, path_utf16) != FR_OK) {
        lv_free(dir);
        return NULL;
    }

    return dir;
}

/**
 * @brief 读取目录项回调函数
 * @param drv LVGL文件系统驱动指针
 * @param dir_p DIR对象指针
 * @param name 输出的目录项名称
 * @param name_size 名称缓冲区大小
 * @return lv_fs_res_t 读取结果
 * @note 目录项以'/'开头，自动跳过'.'与'..'，遍历结束时返回空字符串
 */
static lv_fs_res_t fs_dir_read(lv_fs_drv_t *drv, void *dir_p, char *name,
                               uint32_t name_size)
{
    FILINFO info;
    FRESULT result;

    LV_UNUSED(drv);
    if(name == NULL || name_size == 0U) return LV_FS_RES_INV_PARAM;

    do {
        name[0] = '\0';
        result = f_readdir((DIR *)dir_p, &info);
        if(result != FR_OK) return fatfs_result_to_lvgl(result);
        if(info.fname[0] == 0) return LV_FS_RES_OK;

        if((info.fattrib & AM_DIR) != 0U) {
            if(name_size < 2U) return LV_FS_RES_INV_PARAM;
            name[0] = '/';
            (void)utf16to8(info.fname, (uint8_t *)&name[1], name_size - 1U);
        }
        else {
            (void)utf16to8(info.fname, (uint8_t *)name, name_size);
        }
    } while(lv_strcmp(name, "/.") == 0 || lv_strcmp(name, "/..") == 0);

    return LV_FS_RES_OK;
}

/**
 * @brief 关闭目录回调函数
 * @param drv LVGL文件系统驱动指针
 * @param dir_p DIR对象指针
 * @return lv_fs_res_t 关闭结果
 */
static lv_fs_res_t fs_dir_close(lv_fs_drv_t *drv, void *dir_p)
{
    FRESULT result;

    LV_UNUSED(drv);
    result = f_closedir((DIR *)dir_p);
    lv_free(dir_p);
    return fatfs_result_to_lvgl(result);
}

/**
 * @brief 注册LVGL的FatFs文件系统驱动
 * @return bool true表示注册成功
 * @note 盘符为LVGL_FATFS_LETTER（C:），已注册时直接返回false
 */
bool lvgl_fatfs_init(void)
{
    if(lv_fs_get_drv(LVGL_FATFS_LETTER) != NULL) return false;

    lv_fs_drv_init(&lvgl_fatfs_drv);
    lvgl_fatfs_drv.letter = LVGL_FATFS_LETTER;
    lvgl_fatfs_drv.cache_size = LVGL_FATFS_CACHE_SIZE;
    lvgl_fatfs_drv.ready_cb = fs_ready;
    lvgl_fatfs_drv.open_cb = fs_open;
    lvgl_fatfs_drv.close_cb = fs_close;
    lvgl_fatfs_drv.read_cb = fs_read;
    lvgl_fatfs_drv.write_cb = fs_write;
    lvgl_fatfs_drv.seek_cb = fs_seek;
    lvgl_fatfs_drv.tell_cb = fs_tell;
    lvgl_fatfs_drv.dir_open_cb = fs_dir_open;
    lvgl_fatfs_drv.dir_read_cb = fs_dir_read;
    lvgl_fatfs_drv.dir_close_cb = fs_dir_close;
    lv_fs_drv_register(&lvgl_fatfs_drv);

    return lv_fs_get_drv(LVGL_FATFS_LETTER) == &lvgl_fatfs_drv;
}
