/**
 * @file shell_fs.c
 * @author Letter (nevermindzzt@gmail.com)
 * @brief shell file system support
 * @version 0.1
 * @date 2020-07-22
 *
 * @copyright (c) 2020 Letter
 *
 */
#include "shell_fs.h"
#include "shell.h"

#include "fatfs.h"
#include "get_res.h"
#include "log.h"
#include "Events.h"
#include "fs_misc.h"
#include <string.h>
#include <stdio.h>

ShellFs shellFs __attribute__((section(".DTCM")));
uint8_t shellPathBuffer[SHELL_FS_PATH_BUFFER]  __attribute__((section(".DTCM"))) = "/";

static size_t f_cwd(char *buf, size_t size);
static size_t f_cd(char *dir);
static size_t f_ls(char *dir, char *buffer, size_t maxLen);
static const uint8_t *path_resolve(const uint8_t *path, uint8_t *out, size_t size);


/**
 * @brief 用户初始化shell文件系统支持
 */
void userShellFsInit(void) {
    extern Shell shell;
    shellFs.getcwd = f_cwd;
    shellFs.chdir = f_cd;
    shellFs.listdir = f_ls;

    shellFsInit(&shellFs, (char *)shellPathBuffer, SHELL_FS_PATH_BUFFER);
    shellSetPath(&shell, (char *)shellPathBuffer);
    shellCompanionAdd(&shell, SHELL_COMPANION_ID_FS, &shellFs);
}

// /**
//  * @brief 启动SD卡状态检查任务
//  *
//  * @param argument 任务参数，未使用
//  */
// void Card_Check_Task(void *argument) {
//     (void)argument;
//     FRESULT res;
//
//     // 等待系统初始化完成
//     osEventFlagsWait(System_StatusHandle, SYS_INIT_COMPLETE, osFlagsWaitAny, osWaitForever);
//
//     for(;;) {
//         osDelay(Card_Check_Time);
//         uint32_t current_state = osEventFlagsGet(System_StatusHandle) & SYS_CARD_MOUNTED;
//         if (current_state != 0) {   // 已挂载
//             // 通过尝试获取互斥锁来判断是否有文件正在被使用
//             // 如果无法获取互斥锁（超时），说明有文件正在被操作
//             #if _FS_REENTRANT
//             int ret = ff_req_grant(SDFatFS.sobj);
//             if (ret == 1) {
//                 // 成功获取互斥锁，说明当前没有文件正在被操作
//                 // 立即释放互斥锁
//                 ff_rel_grant(SDFatFS.sobj);
//             } else {
//                 // 无法获取互斥锁，说明有文件正在被操作，跳过本次检查
//                 continue;
//             }
//             #else
//             // 未启用重入性，使用原有的检查方式
//             if (SDFile.obj.fs != 0) {
//                 continue;
//             }
//             #endif
//             // 检查SD卡状态
//             if (BSP_SD_GetCardState() != SD_TRANSFER_OK) {
//                 osEventFlagsClear(System_StatusHandle, SYS_CARD_MOUNTED);
//                 FATFS_UnLinkDriverEx(SDPath, 0);
//                 f_mount(NULL, (const TCHAR*)SDPath, 0);
//                 logInfo("SD card has been removed");
//             }
//         } else {
//             current_state = MY_SDMMC1_SD_Init();
//             if (current_state != HAL_OK) {
//                 HAL_SD_DeInit(&hsd1);
//                 continue;
//             }
//             if (BSP_SD_GetCardState() != SD_TRANSFER_OK) continue;
//             logInfo("SD card has been inserted");
//             retSD = FATFS_LinkDriver(&SD_Driver, SDPath);
//             if(retSD != 0) {
//                 FATFS_UnLinkDriverEx(SDPath, 0);
//                 logWarning("SD card is installed, but link failed");
//                 continue;
//             }
//             res = f_mount(&SDFatFS, (const TCHAR*)SDPath, 1);
//             if (res == FR_OK) {
//                 osEventFlagsSet(System_StatusHandle, SYS_CARD_MOUNTED);
//                 logInfo("SD card mounted success");
//             } else {
//                 logWarning("SD card is installed, but mount failed");
//             }
//         }
//     }
// }

/**
 * @brief 改变当前路径(shell调用)
 *
 * @param dir 路径
 */
static void shellCD(char *dir)
{
    Shell *shell = shellGetCurrent();
    ShellFs *shellFs = shellCompanionGet(shell, SHELL_COMPANION_ID_FS);
    SHELL_ASSERT(shellFs, return);
    if (shellFs->chdir(dir) != 0)
    {
        shellWriteString(shell, "error: ");
        shellWriteString(shell, dir);
        shellWriteString(shell, " is not a directory\r\n");
    }
    shellFs->getcwd(shellFs->info.path, shellFs->info.pathLen);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC)|SHELL_CMD_DISABLE_RETURN,
cd, shellCD, change dir);

/**
 * @brief 列出文件(shell调用)
 */
static void shellLS(void)
{
    size_t count;
    char *buffer;

    Shell *shell = shellGetCurrent();
    ShellFs *shellFs = shellCompanionGet(shell, SHELL_COMPANION_ID_FS);
    SHELL_ASSERT(shellFs, return);

    buffer = SHELL_MALLOC(SHELL_FS_LIST_FILE_BUFFER_MAX);
    SHELL_ASSERT(buffer, return);
    count = shellFs->listdir(shellGetPath(shell), buffer, SHELL_FS_LIST_FILE_BUFFER_MAX);
    (void)count;

    shellWriteString(shell, buffer);

    SHELL_FREE(buffer);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC)|SHELL_CMD_DISABLE_RETURN,
ls, shellLS, list all files);

/**
 * @brief 删除文件(shell调用)
 * @param file 文件名
 */
static void shellRM(char *file) {
	FRESULT res;
	TCHAR wfile[_MAX_LFN + 1];
	uint8_t resolved[SHELL_FS_PATH_BUFFER];
	FILINFO fno;

	if (!FS_Check()) {
		logPrintln("File system is not mounted"); return;
	}

	// 解析路径后转换为UTF-16
	path_resolve((const uint8_t *)file, resolved, sizeof(resolved));
	utf8to16((const uint8_t *)resolved, wfile, sizeof(wfile) / sizeof(TCHAR));

	// 检查类型，目录不允许用rm删除
	res = f_stat(wfile, &fno);
	if (res != FR_OK) {
		logPrintln("Fail to access \"%s\": %s", file, FATFS_GetString(res));
		return;
	}
	if (fno.fattrib & AM_DIR) {
		logPrintln("\"%s\" is a directory", file);
		return;
	}

	res = f_unlink(wfile);
	if (res != FR_OK) {
		logPrintln("Fail to remove file \"%s\": %s", file, FATFS_GetString(res));
		return;
	}
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC)|SHELL_CMD_DISABLE_RETURN,
rm, shellRM, remove file);

/**
 * @brief 删除目录(shell调用)
 * @param file 目录名
 */
static void shellRMDIR(char *file) {
	FRESULT res;
	TCHAR wfile[_MAX_LFN + 1];
	uint8_t resolved[SHELL_FS_PATH_BUFFER];
	FILINFO fno;

	if (!FS_Check()) {
		logPrintln("File system is not mounted"); return;
	}

	// 解析路径后转换为UTF-16
	path_resolve((const uint8_t *)file, resolved, sizeof(resolved));
	utf8to16((const uint8_t *)resolved, wfile, sizeof(wfile) / sizeof(TCHAR));

	// 检查类型，非目录不允许用rmdir删除
	res = f_stat(wfile, &fno);
	if (res != FR_OK) {
		logPrintln("Fail to access \"%s\": %s", file, FATFS_GetString(res));
		return;
	}
	if (!(fno.fattrib & AM_DIR)) {
		logPrintln("\"%s\" is not a directory", file);
		return;
	}

	res = f_rmdir(wfile);
	if (res != FR_OK) {
		logPrintln("Fail to remove directory \"%s\": %s", file, FATFS_GetString(res));
		return;
	}
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC)|SHELL_CMD_DISABLE_RETURN,
rmdir, shellRMDIR, remove directory);

/**
 * @brief 创建目录(shell调用)
 * @param file 目录名
 */
static void shellMKDIR(char *file) {
	FRESULT res;
	TCHAR wfile[_MAX_LFN + 1];
	uint8_t resolved[SHELL_FS_PATH_BUFFER];

	if (!FS_Check()) {
		logPrintln("File system is not mounted"); return;
	}

	// 解析路径后转换为UTF-16并创建目录
	path_resolve((const uint8_t *)file, resolved, sizeof(resolved));
	utf8to16((const uint8_t *)resolved, wfile, sizeof(wfile) / sizeof(TCHAR));
	res = f_mkdir(wfile);
	if (res != FR_OK) {
		logPrintln("Fail to create directory \"%s\": %s", file, FATFS_GetString(res));
		return;
	}
	logPrintln("Created directory \"%s\"", file);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC)|SHELL_CMD_DISABLE_RETURN,
mkdir, shellMKDIR, create directory);

/**
 * @brief 复制文件(shell调用)
 * @param argc 参数个数
 * @param argv 参数列表
 */
static void shellCP(int argc, char *argv[]) {
	FRESULT res;
	UINT br, bw;
	FIL *in = NULL;
	FIL *out = NULL;
	uint8_t *buffer;
	uint8_t resolvedSrc[SHELL_FS_PATH_BUFFER];
	uint8_t resolvedDst[SHELL_FS_PATH_BUFFER];

	if (!FS_Check()) {
		logPrintln("File system is not mounted"); return;
	} else if (argc != 3) {
		logPrintln("Usage: cp <src> <dst>"); return;
	}

	buffer = SHELL_MALLOC(2049);
	SHELL_ASSERT(buffer, return);

	// 源/目标路径解析为绝对路径
	path_resolve((const uint8_t *)argv[1], resolvedSrc, sizeof(resolvedSrc));
	path_resolve((const uint8_t *)argv[2], resolvedDst, sizeof(resolvedDst));

	res = F_open(&in, (const uint8_t *)resolvedSrc, FA_READ);
	if (res != FR_OK) {
		logPrintln("Fail to open source: %s\r\n%s", argv[1], FATFS_GetString(res));
		SHELL_FREE(buffer); return;
	}

	res = F_open(&out, (const uint8_t *)resolvedDst, FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK) {
		logPrintln("Fail to create target: %s\r\n%s", argv[2], FATFS_GetString(res));
		F_close(&in);
		SHELL_FREE(buffer); return;
	}

	while (1) {
		res = f_read(in, buffer, 2048, &br);
		if (res != FR_OK || br == 0) {
			break;
		}

		res = f_write(out, buffer, br, &bw);
		if (res != FR_OK || bw != br) {
			logPrintln("Write error: %s", FATFS_GetString(res));
			break;
		}
	}

	F_close(&in);
	F_close(&out);
	SHELL_FREE(buffer);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
cp, shellCP, copy file);

/**
 * @brief 移动或重命名文件(shell调用)
 * @param argc 参数个数
 * @param argv 参数列表
 */
static void shellMV(int argc, char *argv[]) {
	FRESULT res;
	TCHAR wold[_MAX_LFN + 1];
	TCHAR wnew[_MAX_LFN + 1];
	uint8_t resolvedOld[SHELL_FS_PATH_BUFFER];
	uint8_t resolvedNew[SHELL_FS_PATH_BUFFER];

	if (!FS_Check()) {
		logPrintln("File system is not mounted"); return;
	} else if (argc != 3) {
		logPrintln("Usage: mv <src> <dst>"); return;
	}

	// 路径解析为绝对路径后转换为UTF-16并重命名
	path_resolve((const uint8_t *)argv[1], resolvedOld, sizeof(resolvedOld));
	path_resolve((const uint8_t *)argv[2], resolvedNew, sizeof(resolvedNew));
	utf8to16((const uint8_t *)resolvedOld, wold, sizeof(wold) / sizeof(TCHAR));
	utf8to16((const uint8_t *)resolvedNew, wnew, sizeof(wnew) / sizeof(TCHAR));

	res = f_rename(wold, wnew);
	if (res != FR_OK) {
		logPrintln("Fail to move file: %s\r\n%s", argv[2], FATFS_GetString(res));
		return;
	}
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
mv, shellMV, move or rename file);

/**
 * @brief cat命令包装
 */
static void shellCat(int argc, char *argv[]) {
	if (!FS_Check()) {
		logPrintln("File system is not mounted"); return;
	}

    cat(argc, argv);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
cat, shellCat, cat);

/**
 * @brief hexdump命令包装，实现位于hexdump.c
 */
static void shellHexdump(int argc, char *argv[]) {
	if (!FS_Check()) {
		logPrintln("File system is not mounted"); return;
	}

    hexdump(argc, argv);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
hexdump, shellHexdump, hex dump file);

/**
 * @brief 初始化shell文件系统支持
 *
 * @param shellFs shell文件系统对象
 * @param pathBuffer shell路径缓冲
 * @param pathLen 路径缓冲区大小
 */
void shellFsInit(ShellFs *shellFs, char *pathBuffer, size_t pathLen)
{
    shellFs->info.path = pathBuffer;
    shellFs->info.pathLen = pathLen;
    shellFs->getcwd(shellFs->info.path, pathLen);
}

/**
 * @brief 获取当前工作目录
 * @param buf 路径缓冲区
 * @param size 路径缓冲区大小
 * @return 路径长度
 */
static size_t f_cwd(char *buf, size_t size) {
    TCHAR wch[_MAX_LFN + 1];
    FRESULT res = f_getcwd(wch, sizeof(wch)/sizeof(TCHAR));
    if (res != FR_OK) return 0;

#if _VOLUMES == 1
    // 单卷时f_getcwd不返回盘符，手动补上"0:"
    if (wch[0] == '/') {
        if (size < 3) return 0;
        buf[0] = '0'; buf[1] = ':'; buf[2] = '\0';
        size_t len = utf16to8(wch, (uint8_t *)buf + 2, size - 2);
        return len + 2;
    }
#endif

    return utf16to8(wch, (uint8_t *)buf, size);
}

/**
 * @brief 改变当前路径
 * @param dir 路径
 * @return 0 成功， 1 失败
 */
static size_t f_cd(char *dir) {
    if (!FS_Check()) {
		logPrintln("File system is not mounted"); return 1;
	}

    TCHAR wdir[_MAX_LFN + 1];
    utf8to16((const uint8_t *)dir, wdir, sizeof(wdir)/sizeof(TCHAR));
    FRESULT res = f_chdir(wdir);
    return (res != FR_OK);
}

/**
 * @brief 列出文件
 * @param dir 路径
 * @param buffer 文件列表缓冲区
 * @param maxLen 文件列表缓冲区大小
 * @return 文件列表长度
 */
static size_t f_ls(char *dir, char *buffer, size_t maxLen) {
    DIR dir_obj;
    FILINFO file_info;
    FRESULT res;
    size_t len = 0;
    size_t written;
    uint8_t resolved[SHELL_FS_PATH_BUFFER];

    if (!FS_Check()) {
		logPrintln("File system is not mounted"); return 0;
	}

    // 路径解析为绝对路径后转换为UTF-16
    path_resolve((const uint8_t *)dir, resolved, sizeof(resolved));
    TCHAR wdir[_MAX_LFN + 1];
    utf8to16(resolved, wdir, sizeof(wdir)/sizeof(TCHAR));

    res = f_opendir(&dir_obj, wdir);
    if (res != FR_OK) {
        snprintf(buffer, maxLen, "Failed to open directory: %s\r\n%s\r\n", dir, FATFS_GetString(res));
        return strlen(buffer);
    }

    // 读取目录内容
    while (1) {
        res = f_readdir(&dir_obj, &file_info);
        if (res != FR_OK) {
            if (len < maxLen - 64) {
                snprintf(buffer + len, maxLen - len, "Failed to read directory entry: %s\r\n", FATFS_GetString(res));
                len += strlen(buffer + len);
            } break;
        }
        if (file_info.fname[0] == 0) break;  // 读取完毕

        // 跳过.和..
        if (file_info.fname[0] == '.' && (file_info.fname[1] == 0 || (file_info.fname[1] == '.' && file_info.fname[2] == 0))) {
            continue;
        }

        // 将UTF-16文件名转换为UTF-8
        uint8_t utf8_fname[_MAX_LFN * 3 + 1];
        utf16to8(file_info.fname, utf8_fname, sizeof(utf8_fname));

        // 计算所需的最小缓冲区空间
        size_t fname_len = strlen((const char *)utf8_fname);
        size_t needed_space = fname_len + 64; // 文件名长度 + 额外信息

        // 检查缓冲区空间
        if (len + needed_space >= maxLen) {
            snprintf(buffer + len, maxLen - len, "\r\nBuffer overflow - not enough space for file: %s\r\n", utf8_fname);
            len += strlen(buffer + len);
            break;
        }

        // 添加文件名到缓冲区
        if (file_info.fattrib & AM_DIR) {
            // 目录
            written = snprintf(buffer + len, maxLen - len, "[DIR]  %s\r\n", utf8_fname);
        } else {
            // 文件
            written = snprintf(buffer + len, maxLen - len, "       %s  %lu bytes\r\n", utf8_fname, file_info.fsize);
        }

        // 检查snprintf的返回值，确保写入成功
        if (written >= maxLen - len) {
            snprintf(buffer + len, maxLen - len, "\r\nFailed to write to buffer\r\n");
            len += strlen(buffer + len);
            break;
        }

        len += written;
    }

    f_closedir(&dir_obj);
    return len;
}

/**
 * @brief 路径解析，绝对路径直接使用，相对路径拼接当前工作目录
 * @param path 输入路径
 * @param out 解析结果缓冲区
 * @param size 缓冲区大小
 * @return 解析后的路径
 */
static const uint8_t *path_resolve(const uint8_t *path, uint8_t *out, size_t size) {
	size_t cwdLen;

	if (size == 0) {
		return NULL;
	}

	// FatFS绝对路径以盘符开头: "n:/..."
	if (path[0] >= '0' && path[0] <= '9' && path[1] == ':') {
		snprintf((char *)out, size, "%s", path);
		return out;
	}

	// 相对路径，拼接当前工作目录
	cwdLen = strlen((const char *)shellPathBuffer);
	if (cwdLen > 0 && shellPathBuffer[cwdLen - 1] == '/') {
		snprintf((char *)out, size, "%s%s", shellPathBuffer, path);
	} else {
		snprintf((char *)out, size, "%s/%s", shellPathBuffer, path);
	}
	return out;
}
