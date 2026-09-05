/**
 * @file hexdump.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief 仿照Linux hexdump命令功能实现的嵌入式工具
 */

#include "fs_misc.h"
#include "shell.h"
#include "fatfs.h"
#include "get_res.h"
#include "log.h"
#include "Events.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define HEXD_LINE_BYTES 16	// 每行显示的字节数
#define HEXD_OUT_BUFSZ 128	// 输出缓冲大小

/**
 * @brief 单字符写入输出缓冲，满HEXD_OUT_BUFSZ字节自动发送
 * @param shell shell对象
 * @param outbuf 输出缓冲
 * @param outlen 输出长度指针
 * @param c 字符
 */
static void hexdump_putc(Shell *shell, uint8_t *outbuf, uint8_t *outlen, uint8_t c) {
	outbuf[(*outlen)++] = c;
	if (*outlen >= HEXD_OUT_BUFSZ) {
		shell->write((char *)outbuf, *outlen);
		*outlen = 0;
	}
}

/**
 * @brief 字符串写入输出缓冲
 * @param shell shell对象
 * @param outbuf 输出缓冲
 * @param outlen 输出长度指针
 * @param str 字符串
 */
static void hexdump_puts(Shell *shell, uint8_t *outbuf, uint8_t *outlen, const char *str) {
	while (*str) {
		hexdump_putc(shell, outbuf, outlen, (uint8_t)*str++);
	}
}

/**
 * @brief 输出一行十六进制+ASCII
 * @param shell shell对象
 * @param outbuf 输出缓冲
 * @param outlen 输出长度指针
 * @param line 行数据
 * @param offset 文件偏移
 * @param valid 有效字节数(最后一行可能不足16)
 */
static void hexdump_dump_line(Shell *shell, uint8_t *outbuf, uint8_t *outlen, const uint8_t *line, uint32_t offset, uint8_t valid) {
	char tmp[16];
	uint8_t i;

	// 输出文件偏移(8位十六进制)
	snprintf(tmp, sizeof(tmp), "%08lX  ", (unsigned long)offset);
	hexdump_puts(shell, outbuf, outlen, tmp);

	// 十六进制列，第8字节处额外空格分隔
	for (i = 0; i < HEXD_LINE_BYTES; i++) {
		if (i == 8) {
			hexdump_puts(shell, outbuf, outlen, " ");
		}
		if (i < valid) {
			snprintf(tmp, sizeof(tmp), "%02X ", (unsigned int)line[i]);
		} else {
			snprintf(tmp, sizeof(tmp), "   ");
		}
		hexdump_puts(shell, outbuf, outlen, tmp);
	}

	// ASCII列，不可打印字符显示为'.'
	hexdump_puts(shell, outbuf, outlen, " |");
	for (i = 0; i < valid; i++) {
		uint8_t c = line[i];
		hexdump_putc(shell, outbuf, outlen, (c >= 0x20 && c <= 0x7E) ? c : '.');
	}
	hexdump_puts(shell, outbuf, outlen, "|\r\n");
}

/**
 * @brief 以十六进制+ASCII格式显示文件内容
 * @param file 文件名
 * @param skip 跳过的起始字节数
 * @param limit 显示的最大字节数，0表示不限制
 */
static void hexdump_show(const uint8_t *file, uint32_t skip, uint32_t limit) {
	FRESULT res;
	UINT bytesRead;
	Shell *shell = shellGetCurrent();
	FIL *fp = NULL;
	uint8_t *buffer;
	uint8_t outbuf[HEXD_OUT_BUFSZ];
	uint8_t outlen = 0;
	uint8_t line[HEXD_LINE_BYTES];
	uint8_t lineLen = 0;
	uint32_t offset = skip;		// 文件偏移，从跳过位置开始
	uint32_t remaining = limit;	// 剩余需显示的字节数，0表示不限制
	UINT n;

	buffer = SHELL_MALLOC(4097);
	SHELL_ASSERT(buffer, return);

	res = F_open(&fp, file, FA_READ);
	if (res != FR_OK) {
		logPrintln("Fail to open file: %s\r\n%s", file, FATFS_GetString(res));
		SHELL_FREE(buffer); return;
	}

	// 跳过起始偏移
	if (skip > 0) {
		res = f_lseek(fp, skip);
		if (res != FR_OK) {
			logPrintln("Fail to seek: %s", FATFS_GetString(res));
			F_close(&fp);
			SHELL_FREE(buffer);
			return;
		}
	}

	osEventFlagsSet(System_StatusHandle, APP_NEED_USART);
	while (1) {
		UINT toRead = 4096;

		// 按-n限制剩余字节
		if (limit > 0 && remaining < toRead) {
			toRead = (UINT)remaining;
		}
		if (toRead == 0) {
			break;
		}

		res = f_read(fp, buffer, toRead, &bytesRead);
		if (res != FR_OK || bytesRead == 0) {
			break;
		}

		for (n = 0; n < bytesRead; n++) {
			line[lineLen++] = buffer[n];
			if (lineLen == HEXD_LINE_BYTES) {
				hexdump_dump_line(shell, outbuf, &outlen, line, offset, HEXD_LINE_BYTES);
				offset += HEXD_LINE_BYTES;
				lineLen = 0;
			}
		}

		// 更新剩余字节数
		if (limit > 0) {
			remaining -= bytesRead;
			if (remaining == 0) {
				break;
			}
		}
	}

	// 输出末尾不足一行的剩余数据
	if (lineLen > 0) {
		hexdump_dump_line(shell, outbuf, &outlen, line, offset, lineLen);
	}

	// 发送输出缓冲中剩余数据
	if (outlen > 0) {
		shell->write((char *)outbuf, outlen);
	}

	F_close(&fp);
	SHELL_FREE(buffer);
	osEventFlagsClear(System_StatusHandle, APP_NEED_USART);
}

/**
 * @brief 显示hexdump工具帮助信息
 */
static void hexdump_help(void) {
	logPrintln(
		"Usage: hexdump [OPTION] FILE\r\n"
		"  -h, --help    show this help\r\n"
		"  -C            hex + ASCII display (default)\r\n"
		"  -s OFFSET     skip OFFSET bytes from the start\r\n"
		"  -n LENGTH     display only LENGTH bytes"
	);
}

/**
 * @brief hexdump工具主函数，解析指令并分发
 * @param argc 参数个数
 * @param argv 参数列表
 */
void hexdump(int argc, char *argv[]) {
	const uint8_t *file = NULL;
	uint32_t skip = 0;
	uint32_t limit = 0;
	uint8_t i;

	if (argc < 2) {
		hexdump_help();
		return;
	}

	// 解析参数：选项和文件
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1] != '\0') {
			const char *opt = argv[i];
			const char *arg;
			uint32_t *dst;

			// 仅支持-C(与默认输出一致)
			if (strcmp(opt, "-C") == 0) {
				continue;
			}
			if (strcmp(opt, "-h") == 0 || strcmp(opt, "--help") == 0) {
				hexdump_help();
				return;
			}
			// 带参数选项: -s偏移 -n长度，支持"-s100"粘连形式
			if (opt[1] == 's' || opt[1] == 'n') {
				dst = (opt[1] == 's') ? &skip : &limit;
				if (opt[2] != '\0') {
					// 粘连形式
					arg = &opt[2];
				} else {
					// 分离形式，取下一参数
					if (i + 1 >= argc) {
						logPrintln("hexdump: option '%c' requires an argument", opt[1]);
						hexdump_help();
						return;
					}
					arg = argv[++i];
				}
				*dst = (uint32_t)strtoul(arg, NULL, 0);
				continue;
			}
			logPrintln("hexdump: invalid option -- '%s'", opt);
			hexdump_help();
			return;
		}
		// 文件参数，仅支持一个
		if (file != NULL) {
			logPrintln("hexdump: too many files");
			return;
		}
		file = (const uint8_t *)argv[i];
	}

	if (file == NULL) {
		hexdump_help();
		return;
	}

	// 显示文件十六进制内容
	hexdump_show(file, skip, limit);
}
