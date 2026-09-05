/**
 * @file get_res.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief 返回值格式化工具源文件
 */

#include "get_res.h"


/**
 * @brief 格式化FATFS返回值
 * @param res 返回值
 * @return 格式化后的字符串
 */
const char* FATFS_GetString(FRESULT res) {
  switch(res) {
    case FR_OK:                   return "Succeeded";
    case FR_DISK_ERR:             return "A hard error occurred in the low level disk I/O layer";
    case FR_INT_ERR:              return "Assertion failed";
    case FR_NOT_READY:            return "The physical drive cannot work";
    case FR_NO_FILE:              return "Could not find the file";
    case FR_NO_PATH:              return "Could not find the path";
    case FR_INVALID_NAME:         return "The path name format is invalid";
    case FR_DENIED:               return "Access denied due to prohibited access or directory full";
    case FR_EXIST:                return "Access denied due to prohibited access or file exists";
    case FR_INVALID_OBJECT:       return "The file/directory object is invalid";
    case FR_WRITE_PROTECTED:      return "The physical drive is write protected";
    case FR_INVALID_DRIVE:        return "The logical drive number is invalid";
    case FR_NOT_ENABLED:          return "The volume has no work area";
    case FR_NO_FILESYSTEM:        return "There is no valid FAT volume";
    case FR_MKFS_ABORTED:         return "The f_mkfs() aborted due to any parameter error";
    case FR_TIMEOUT:              return "Could not get a grant to access the volume within defined period";
    case FR_LOCKED:               return "The operation is rejected according to the file sharing policy";
    case FR_NOT_ENOUGH_CORE:      return "LFN working buffer could not be allocated";
    case FR_TOO_MANY_OPEN_FILES:  return "Number of open files > _FS_LOCK";
    case FR_INVALID_PARAMETER:    return "Given parameter is invalid";
    default:                      return "Unknown result";
  }
}

/**
 * @brief 格式化文件名检查返回值
 * @param res 返回值
 * @return 格式化后的字符串
 */
const char* NameCheck_GetString(NameCheck_t res) {
  switch (res) {
    case FIL_PASS:              return "Check Passed";
    case FIL_TYPE_ERROR:        return "File type does not match";
    case FIL_NAME_NULL:         return "File name is null or empty";
    case FIL_TOO_LONG:          return "File name is too long";
    case FIL_EXTENSION_ERROR:   return "File extension format error";
    case FIL_NAME_INVALID:      return "File name contains invalid characters";
    default:                    return "Unknown result";
  }
}
