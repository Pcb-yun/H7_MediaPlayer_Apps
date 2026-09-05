/**
 * @file cat.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief 仿照Linux cat命令功能实现的嵌入式工具
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

#define CAT_MAX_FILES 6	// 一次最多处理的输入文件数量
#define CAT_OUT_BUFSZ 128	// 显示输出缓冲大小

// cat选项标志位
#define CAT_FLAG_NUMBER    (1 << 0)	// -n 输出行号
#define CAT_FLAG_SQUEEZE   (1 << 1)	// -s 压缩连续空行
#define CAT_FLAG_SHOW_END  (1 << 2)	// -E 行尾显示$

/**
 * @brief 单字符写入输出缓冲，满CAT_OUT_BUFSZ字节自动发送
 * @param shell shell对象
 * @param outbuf 输出缓冲
 * @param outlen 输出长度指针
 * @param c 字符
 */
static void cat_putc(Shell *shell, uint8_t *outbuf, uint8_t *outlen, uint8_t c) {
	outbuf[(*outlen)++] = c;
	if (*outlen >= CAT_OUT_BUFSZ) {
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
static void cat_puts(Shell *shell, uint8_t *outbuf, uint8_t *outlen, const char *str) {
	while (*str) {
		cat_putc(shell, outbuf, outlen, (uint8_t)*str++);
	}
}

/**
 * @brief 显示文件内容
 * @param file 文件名
 * @param flags 选项标志位
 */
static void cat_show(const uint8_t *file, uint8_t flags) {
	FRESULT res;
	UINT bytesRead;
	Shell *shell = shellGetCurrent();
	FIL *fp = NULL;
	uint8_t *buffer;
	uint8_t outbuf[CAT_OUT_BUFSZ];
	uint8_t outlen = 0;
	uint8_t *p;
	UINT n;
	uint32_t lineNum = 0;	// 行号计数
	bool atLineStart = true;	// 当前位于行首
	bool prevBlank = false;		// 上一行是否为空行
	char numbuf[16];

	buffer = SHELL_MALLOC(2049);
	SHELL_ASSERT(buffer, return);

	res = F_open(&fp, file, FA_READ);
	if (res != FR_OK) {
		logPrintln("Fail to open file: %s\r\n%s", file, FATFS_GetString(res));
		SHELL_FREE(buffer); return;
	}

	osEventFlagsSet(System_StatusHandle, APP_NEED_USART);

	if (flags == 0) {
		// 无选项: 整块原样输出，保持最高性能
		while (1) {
			res = f_read(fp, buffer, 2048, &bytesRead);
			if (res != FR_OK || bytesRead == 0) {
				break;
			}

			buffer[bytesRead] = '\0';
			shellWriteString(shell, (char *)buffer);
		}
	} else {
		// 有选项: 逐字节状态机处理行
		while (1) {
			res = f_read(fp, buffer, 2048, &bytesRead);
			if (res != FR_OK || bytesRead == 0) {
				break;
			}

			for (p = buffer, n = 0; n < bytesRead; n++, p++) {
				uint8_t c = *p;

				if (c == '\n') {
					// 行结束
					if (atLineStart) {
						// 空行
						if ((flags & CAT_FLAG_SQUEEZE) && prevBlank) {
							continue;	// 压缩连续空行
						}
						if (flags & CAT_FLAG_NUMBER) {
							snprintf(numbuf, sizeof(numbuf), "%6lu\t", (unsigned long)(++lineNum));
							cat_puts(shell, outbuf, &outlen, numbuf);
						}
						if (flags & CAT_FLAG_SHOW_END) {
							cat_puts(shell, outbuf, &outlen, "$");
						}
						cat_puts(shell, outbuf, &outlen, "\r\n");
						prevBlank = true;
					} else {
						if (flags & CAT_FLAG_SHOW_END) {
							cat_puts(shell, outbuf, &outlen, "$");
						}
						cat_puts(shell, outbuf, &outlen, "\r\n");
						prevBlank = false;
					}
					atLineStart = true;
					continue;
				}
				if (c == '\r') {
					continue;	// 忽略CR，兼容CRLF
				}

				// 行首输出行号
				if (atLineStart) {
					if (flags & CAT_FLAG_NUMBER) {
						snprintf(numbuf, sizeof(numbuf), "%6lu\t", (unsigned long)(++lineNum));
						cat_puts(shell, outbuf, &outlen, numbuf);
					}
					atLineStart = false;
				}
				cat_putc(shell, outbuf, &outlen, c);
			}
		}

		// 文件末尾最后一行无换行时补换行
		if (!atLineStart) {
			if (flags & CAT_FLAG_SHOW_END) {
				cat_puts(shell, outbuf, &outlen, "$");
			}
			cat_puts(shell, outbuf, &outlen, "\r\n");
		}
		// 发送输出缓冲中剩余数据
		if (outlen > 0) {
			shell->write((char *)outbuf, outlen);
		}
	}

	F_close(&fp);
	SHELL_FREE(buffer);
	osEventFlagsClear(System_StatusHandle, APP_NEED_USART);
}

/**
 * @brief 从串口输入写入文件，直到^D (EOF)退出
 * @param file 输出文件名
 * @param append true:追加写入 false:覆盖写入
 */
static void cat_input(const uint8_t *file, bool append) {
	FRESULT res;
	UINT bytesWritten;
	Shell *shell = shellGetCurrent();
	FIL *fp = NULL;
	char ch;

	res = F_open(&fp, file, FA_WRITE | (append ? FA_OPEN_APPEND : FA_CREATE_ALWAYS));
	if (res != FR_OK) {
		logPrintln("Fail to create file: %s\r\n%s", file, FATFS_GetString(res));
		return;
	}

	osEventFlagsSet(System_StatusHandle, APP_NEED_USART);
	while (1) {
		const char *writeData;
		UINT writeLen;

		if (shell->read(&ch, 1) == 1) {
			if (ch == 0x04) break;
			else if (ch == '\r') {
				shell->write("\r\n", 2);
				writeData = "\r\n"; writeLen = 2;
			} else {
				shell->write(&ch, 1);
				writeData = &ch; writeLen = 1;
			}

			res = f_write(fp, writeData, writeLen, &bytesWritten);
			if (res != FR_OK || bytesWritten != writeLen) {
				logPrintln("Write error: %s", FATFS_GetString(res));
				break;
			}
		}
		osDelay(1);
	}

	F_close(&fp);
	osEventFlagsClear(System_StatusHandle, APP_NEED_USART);
}

/**
 * @brief 合并多个输入文件写入输出文件
 * @param files 输入文件列表
 * @param fileCount 输入文件数量
 * @param outFile 输出文件名
 * @param append true:追加写入 false:覆盖写入
 */
static void cat_copy(const uint8_t *files[], uint8_t fileCount, const uint8_t *outFile, bool append) {
	FRESULT res;
	UINT bytesRead;
	UINT bytesWritten;
	FIL *in = NULL;
	FIL *out = NULL;
	uint8_t *buffer;
	uint8_t i;

	// 打开输出文件，覆盖或追加
	res = F_open(&out, outFile, FA_WRITE | (append ? FA_OPEN_APPEND : FA_CREATE_ALWAYS));
	if (res != FR_OK) {
		logPrintln("Fail to create file: %s\r\n%s", outFile, FATFS_GetString(res));
		return;
	}

	buffer = SHELL_MALLOC(2049);
	if (buffer == NULL) {
		F_close(&out); return;
	}

	// 依次读取每个输入文件并写入输出文件
	for (i = 0; i < fileCount; i++) {
		in = NULL;
		res = F_open(&in, files[i], FA_READ);
		if (res != FR_OK) {
			logPrintln("Fail to open file: %s\r\n%s", files[i], FATFS_GetString(res));
			continue;
		}

		while (1) {
			res = f_read(in, buffer, 2048, &bytesRead);
			if (res != FR_OK || bytesRead == 0) {
				break;
			}

			res = f_write(out, buffer, bytesRead, &bytesWritten);
			if (res != FR_OK || bytesWritten != bytesRead) {
				logPrintln("Write error: %s", FATFS_GetString(res));
				break;
			}
		}

		F_close(&in);
	}

	SHELL_FREE(buffer);
}

/**
 * @brief 显示cat工具帮助信息
 */
static void cat_help(void) {
	logPrintln(
		"Usage: cat [OPTION] FILE... [> | >> OUTFILE]\r\n"
		"  -h, --help       show this help\r\n"
		"  -n               number all output lines\r\n"
		"  -s               squeeze repeated empty lines\r\n"
		"  -E               display $ at end of each line\r\n"
		"  FILE             display file content\r\n"
		"  > OUTFILE        redirect serial input to file, overwrite\r\n"
		"  >> OUTFILE       redirect serial input to file, append\r\n"
		"  FILE... > OUT    concatenate files to OUT, overwrite\r\n"
		"  FILE... >> OUT   concatenate files to OUT, append"
	);
}

/**
 * @brief cat工具主函数，解析指令并分发
 * @param argc 参数个数
 * @param argv 参数列表
 */
void cat(int argc, char *argv[]) {
	const uint8_t *files[CAT_MAX_FILES];	// 输入文件列表
	const uint8_t *outFile = NULL;			// 重定向输出文件
	bool appendFlag = false;				// 重定向追加标志
	uint8_t flags = 0;						// 选项标志位
	uint8_t fileCount = 0;
	uint8_t i;
	uint8_t k;

	if (argc < 2) {
		cat_help(); return;
	}

	// 通用参数解析：区分选项、重定向符">"和位置参数(文件)
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '>') {
			// 重定向符: ">"覆盖, ">>"追加
			appendFlag = (argv[i][1] == '>');
			if (argv[i][appendFlag ? 2 : 1] == '\0') {
				// 分离形式: "> out" / ">> out"，输出文件为下一个参数
				if (i + 2 != argc) {
					cat_help(); return;
				}
				outFile = (const uint8_t *)argv[i + 1];
				i++;
			} else {
				// 粘连形式: ">out" / ">>out"，输出文件名内嵌在参数中
				if (i + 1 != argc) {
					cat_help(); return;
				}
				outFile = (const uint8_t *)&argv[i][appendFlag ? 2 : 1];
			} continue;
		} else if (argv[i][0] == '-' && argv[i][1] != '\0') {
			if (argv[i][1] == '-') {
				// 长选项
				if (strcmp(argv[i], "--help") == 0) {
					cat_help(); return;
				}
				logPrintln("invalid option -- '%s'", argv[i]);
				cat_help(); return;
			}
			// 短选项，支持组合形式: -nsE
			for (k = 1; argv[i][k] != '\0'; k++) {
				switch (argv[i][k]) {
					case 'n': flags |= CAT_FLAG_NUMBER; break;
					case 's': flags |= CAT_FLAG_SQUEEZE; break;
					case 'E': flags |= CAT_FLAG_SHOW_END; break;
					case 'h': cat_help(); return;
					default:
						logPrintln("invalid option -- '%c'", argv[i][k]);
						cat_help(); return;
				}
			}
		} else {
			// 位置参数，视为输入文件
			if (fileCount >= CAT_MAX_FILES) {
				logPrintln("too many files");
				cat_help(); return;
			}
			files[fileCount++] = (const uint8_t *)argv[i];
		}
	}

	// 重定向模式
	if (outFile != NULL) {
		if (fileCount > 0) {
			// 合并输入文件写入输出文件
			cat_copy(files, fileCount, outFile, appendFlag);
			return;
		}
		// 从串口输入写入输出文件
		cat_input(outFile, appendFlag);
		return;
	}

	// 依次显示每个输入文件
	for (i = 0; i < fileCount; i++) {
		cat_show(files[i], flags);
	}
}
