/**
 * @file xymodem_port.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief XYmodem协议用户接口源文件
 */

#include "xymodem_port.h"
#include "shell.h"
#include "Events.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "fatfs.h"
#include "log.h"
#include "usart.h"
#include "crc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>


/* XYmodem 端口上下文 */
struct XYM_Port_t {
	Shell *shell;
	xym_session_t session;
	uint8_t *packet_buf;	// 协议包缓冲
	uint8_t *file_cache;	// 文件收发缓冲
	uint32_t cache_size;	// 文件收发缓冲大小
};
static struct XYM_Port_t *port = NULL;


static xym_sta_t xymodem_port_send_data(const uint8_t *data, const uint32_t cnt, const uint32_t tick);
static xym_sta_t xymodem_port_recv_data(uint8_t *data, const uint32_t cnt, const uint32_t tick);
uint16_t xymodem_port_crc16(const uint8_t *data, const uint32_t cnt);

/**
 * @brief  取路径中的文件名
 * @param  path 路径
 * @return 文件名指针
 */
static const char *xym_file_basename(const char *path) {
	const char *p = strrchr(path, '/');
	const char *q = strrchr(path, '\\');
	if (q > p) p = q;
	return (p) ? p + 1 : path;
}

/**
 * @brief  编码 Ymodem 文件信息包
 * @param  buff 数据缓冲（128 Bytes）
 * @param  name 文件名
 * @param  size 文件大小 (Bytes)
 */
static void xym_file_encode(uint8_t *buff, const char *name, uint32_t size) {
	uint16_t i;
	for (i = 0; name[i] && i < XYM_PKT_SIZE_128 - 16; ++i) {
		buff[i] = (uint8_t)name[i];
	}
	buff[i++] = '\0';
	snprintf((char *)&buff[i], XYM_PKT_SIZE_128 - i, "%lu", (unsigned long)size);
}

/**
 * @brief  解码 Ymodem 文件信息包
 * @param  buff 数据缓冲（128 Bytes）
 * @param  name 文件名输出
 * @param  size 文件大小输出 (Bytes)
 */
static void xym_file_decode(const uint8_t *buff, char *name, uint32_t *size) {
	uint16_t i = 0;
	while (buff[i] && i < XYM_PKT_SIZE_128 - 1) {
		name[i] = (char)buff[i];
		++i;
	}
	name[i] = '\0';
	if (buff[i] == 0) ++i; /* 跳过分隔符 '\0' */
	*size = (uint32_t)strtoul((const char *)&buff[i], NULL, 10);
}

/**
 * @brief  删除文件（接收失败时清理残留）
 * @param  path 文件路径
 */
static void xym_file_remove(const char *path) {
	TCHAR wpath[512];
	utf8to16((const uint8_t *)path, wpath, sizeof(wpath) / sizeof(TCHAR));
	f_unlink(wpath);
}

/**
 * @brief  将一个通过协议校验的数据包完整写入文件
 * @param  fp  文件句柄
 * @param  buf 已通过 CRC 校验的数据包
 * @param  cnt 待写入字节数
 * @retval FR_OK 成功，否则失败
 */
static FRESULT xym_file_write(FIL *fp, const uint8_t *buf, uint32_t cnt) {
	UINT bw = 0;
	if (cnt == 0) return FR_OK;
	if (f_write(fp, buf, cnt, &bw) != FR_OK || bw != cnt) return FR_INT_ERR;
	return FR_OK;
}

/**
 * @brief  将文件缓存中的有效数据写入文件
 * @param  fp     文件句柄
 * @param  filled 文件缓存当前有效字节数，刷新成功后清零
 * @retval FR_OK  刷新成功
 * @retval 其它    FatFs 写入失败
 */
static FRESULT xym_file_cache_flush(FIL *fp, uint32_t *filled) {
	FRESULT res = xym_file_write(fp, port->file_cache, *filled);
	if (res == FR_OK) *filled = 0;
	return res;
}

/**
 * @brief  将协议数据包追加到文件缓存
 * @note   剩余空间不足时先刷新已有数据，再复制当前数据包。
 *         调用方需保证单包长度不超过文件缓存容量。
 * @param  fp     文件句柄
 * @param  data   已通过协议校验的数据包
 * @param  cnt    数据包有效字节数
 * @param  filled 文件缓存当前有效字节数，追加成功后同步更新
 * @retval FR_OK  追加成功
 * @retval 其它    缓存刷新或文件写入失败
 */
static FRESULT xym_file_cache_append(FIL *fp, const uint8_t *data, uint32_t cnt, uint32_t *filled) {
	if (cnt == 0) return FR_OK;
	if (*filled + cnt > port->cache_size) {
		if (xym_file_cache_flush(fp, filled) != FR_OK) return FR_INT_ERR;
	}
	memcpy(&port->file_cache[*filled], data, cnt);
	*filled += cnt;
	return FR_OK;
}

/**
 * @brief  端口初始化
 * @retval enum xym_sta
 */
static xym_sta_t xym_port_init(void) {
	if (port == NULL) {
		port = pvPortMalloc(sizeof(struct XYM_Port_t));
		SHELL_ASSERT(port, return XYM_ERROR_HW);
	}
	memset(port, 0, sizeof(struct XYM_Port_t));
	port->shell = shellGetCurrent();
	SHELL_ASSERT(port->shell, return XYM_ERROR_HW);

	/* 从最大连续空闲块扣除保留内存，并按 1024 字节向下对齐。 */
	HeapStats_t heap_stats;
	vPortGetHeapStats(&heap_stats);
	size_t alloc_size = 0;
	if (heap_stats.xSizeOfLargestFreeBlockInBytes > XYMODEM_RESERVED_MEM) {
		alloc_size = (heap_stats.xSizeOfLargestFreeBlockInBytes - XYMODEM_RESERVED_MEM) /
			XYM_PKT_SIZE_1024 * XYM_PKT_SIZE_1024;
	}
	/* 至少需要 1 KB 协议包缓冲和 1 KB 文件缓存。 */
	if (alloc_size < XYM_PKT_SIZE_1024 * 2u) {
		logPrintln("no enough memory for xymodem file cache");
		vPortFree(port);
		port = NULL;
		return XYM_ERROR_HW;
	}

	/* 一次分配后切成两个互不重叠的逻辑缓冲，避免堆碎片。 */
	port->packet_buf = pvPortMalloc(alloc_size);
	if (port->packet_buf == NULL) {
		logPrintln("xymodem buffer alloc failed");
		vPortFree(port);
		port = NULL;
		return XYM_ERROR_HW;
	}
	port->file_cache = &port->packet_buf[XYM_PKT_SIZE_1024];
	port->cache_size = (uint32_t)(alloc_size - XYM_PKT_SIZE_1024);

	static const struct xym_ops ops = {
		.send = xymodem_port_send_data,
		.recv = xymodem_port_recv_data,
		.crc16 = xymodem_port_crc16,
	};
	static const struct xym_param param = {
		.send_timeout = 1000,
		.recv_timeout = 1000,
		.error_max_retry = 10,
	};
	xymodem_session_init(&port->session, ops, param);
	return XYM_OK;
}

/**
 * @brief  端口释放
 */
static void xym_port_deinit(void) {
	if (port) {
		/* file_cache 是 packet_buf 所指连续内存中的偏移地址，不单独释放。 */
		vPortFree(port->packet_buf);
		vPortFree(port);
		port = NULL;
	}
}

#if XYMODEM_USE_XMODEM

/**
 * @brief  X协议发送
 */
static void shell_sx(int argc, char *argv[]) {
	if (argc < 2) { logPrintln("Usage: sx <file>"); return; }

	FIL *fp = NULL;
	if (F_open(&fp, (const uint8_t *)argv[1], FA_READ) != FR_OK) {
		logPrintln("Fail to open %s", argv[1]);
		return;
	}

	FILINFO fno;
	TCHAR wpath[512];
	utf8to16((const uint8_t *)argv[1], wpath, sizeof(wpath) / sizeof(TCHAR));
	uint32_t fsize = 0;
	if (f_stat(wpath, &fno) == FR_OK) {
		fsize = (uint32_t)fno.fsize;
	}

	osEventFlagsSet(System_StatusHandle, APP_NEED_USART);
	if (xym_port_init() != XYM_OK) {
		logPrintln("X/Y modem port init failed");
		osEventFlagsClear(System_StatusHandle, APP_NEED_USART);
		return;
	}
	xmodem_init(&port->session);

	xym_sta_t sta = XYM_OK;
	uint16_t len = 0;
	uint32_t sent = 0;
	uint32_t cached = 0;
	uint32_t offset = 0;
	UINT br = 0;

	while (sent < fsize) {
		uint32_t request = fsize - sent;
		if (request > port->cache_size) request = port->cache_size;
		if (f_read(fp, port->file_cache, request, &br) != FR_OK || br == 0) {
			sta = XYM_ERROR_HW;
			break;
		}
		cached = br;
		offset = 0;
		while (offset < cached) {
			uint32_t remain = cached - offset;
			len = (remain > XYM_PKT_SIZE_1024) ? XYM_PKT_SIZE_1024 : (uint16_t)remain;
			sta = xmodem_transmit(&port->session, &port->file_cache[offset], len);
			if (sta != XYM_OK) break;
			offset += len;
			sent += len;
		}
		if (sta != XYM_OK) break;
	}
	if (sta == XYM_OK) sta = xmodem_transmit(&port->session, port->packet_buf, 0);

	F_close(&fp);
	xym_port_deinit();
	osEventFlagsClear(System_StatusHandle, APP_NEED_USART);

	if (sta != XYM_END) logPrintln("Xmodem send failed, code %d", (int)sta);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
sx, shell_sx, Send Xmodem);

/**
 * @brief  X协议接收
 */
static void shell_rx(int argc, char *argv[]) {
	if (argc < 2) { logPrintln("Usage: rx <file>"); return; }

	osEventFlagsSet(System_StatusHandle, APP_NEED_USART);
	if (xym_port_init() != XYM_OK) {
		logPrintln("X/Y modem port init failed");
		osEventFlagsClear(System_StatusHandle, APP_NEED_USART);
		return;
	}
	xmodem_init(&port->session);

	FIL *fp = NULL;
	xym_sta_t sta = XYM_OK;
	uint16_t len = 0;
	uint32_t filled = 0;

	if (F_open(&fp, (const uint8_t *)argv[1], FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
		logPrintln("Fail to create %s", argv[1]);
		xym_port_deinit();
		osEventFlagsClear(System_StatusHandle, APP_NEED_USART);
		return;
	}

	while (1) {
		sta = xmodem_receive(&port->session, port->packet_buf, &len);
		if (sta == XYM_OK) {
			if (xym_file_cache_append(fp, port->packet_buf, len, &filled) != FR_OK) {
				xymodem_active_cancel(&port->session);
				sta = XYM_ERROR_HW;
				break;
			}
			continue;
		}
		break; /* XYM_END 正常结束 / 其它错误 */
	}

	if (sta == XYM_END && xym_file_cache_flush(fp, &filled) != FR_OK) sta = XYM_ERROR_HW;
	if (F_close(&fp) != FR_OK) sta = XYM_ERROR_HW;
	xym_port_deinit();
	osEventFlagsClear(System_StatusHandle, APP_NEED_USART);

	if (sta != XYM_END) {
		xym_file_remove(argv[1]);
		logPrintln("Xmodem receive failed, code %d", (int)sta);
	}
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
rx, shell_rx, Receive Xmodem);

#endif

#if XYMODEM_USE_YMODEM

/**
 * @brief  Y协议发送
 */
static void shell_sb(int argc, char *argv[]) {
	if (argc < 2) { logPrintln("Usage: sb <file1> [file2 ...]"); return; }

	osEventFlagsSet(System_StatusHandle, APP_NEED_USART);
	if (xym_port_init() != XYM_OK) {
		logPrintln("X/Y modem port init failed");
		osEventFlagsClear(System_StatusHandle, APP_NEED_USART);
		return;
	}
	ymodem_init(&port->session);

	xym_sta_t sta = XYM_OK;
	uint16_t len = 0;
	uint32_t sent = 0;
	uint32_t total_sent = 0;
	uint32_t cached = 0;
	uint32_t offset = 0;
	UINT br = 0;
	int fi = 0;
	bool all_ok = true;

	/* Ymodem Batch：逐文件发送文件信息包 + 数据 + EOT */
	for (fi = 1; fi < argc; ++fi) {
		FIL *fp = NULL;
		if (F_open(&fp, (const uint8_t *)argv[fi], FA_READ) != FR_OK) {
			logPrintln("Fail to open %s", argv[fi]);
			all_ok = false;
			break;
		}

		FILINFO fno;
		TCHAR wpath[512];
		utf8to16((const uint8_t *)argv[fi], wpath, sizeof(wpath) / sizeof(TCHAR));
		uint32_t fsize = 0;
		if (f_stat(wpath, &fno) == FR_OK) {
			fsize = (uint32_t)fno.fsize;
		}

		/* 首包：文件信息包（文件名 + 文件大小） */
		memset(port->packet_buf, 0, XYM_PKT_SIZE_128);
		xym_file_encode(port->packet_buf, xym_file_basename(argv[fi]), fsize);
		sta = ymodem_transmit(&port->session, port->packet_buf, XYM_PKT_SIZE_128);
		if (sta != XYM_OK) { F_close(&fp); break; }

		sent = 0;
		while (sent < fsize) {
			uint32_t request = fsize - sent;
			if (request > port->cache_size) request = port->cache_size;
			if (f_read(fp, port->file_cache, request, &br) != FR_OK || br == 0) {
				sta = XYM_ERROR_HW;
				break;
			}
			cached = br;
			offset = 0;
			while (offset < cached) {
				uint32_t remain = cached - offset;
				len = (remain > XYM_PKT_SIZE_1024) ? XYM_PKT_SIZE_1024 : (uint16_t)remain;
				sta = ymodem_transmit(&port->session, &port->file_cache[offset], len);
				if (sta != XYM_OK) break;
				offset += len;
				sent += len;
			}
			if (sta != XYM_OK) break;
		}
		F_close(&fp);
		if (sta != XYM_OK) break;

		/* 数据发完：EOT 结束当前文件 */
		sta = ymodem_transmit(&port->session, port->packet_buf, 0);
		total_sent += sent;
		/* sta 为 XYM_FIL_SET（接收端期待下一个文件）或 XYM_END（会话结束）或错误 */
		if (sta != XYM_FIL_SET) break;
	}

	/* 所有文件发完：若接收端仍期待下一个文件，发空文件信息包结束整个会话 */
	if (sta == XYM_FIL_SET) {
		memset(port->packet_buf, 0, XYM_PKT_SIZE_128);
		sta = ymodem_transmit(&port->session, port->packet_buf, 0);
	}

	xym_port_deinit();
	osEventFlagsClear(System_StatusHandle, APP_NEED_USART);

	if (sta != XYM_END || !all_ok) logPrintln("Ymodem send failed, code %d", (int)sta);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
sb, shell_sb, Send Ymodem);

/**
 * @brief  Y协议接收
 */
static void shell_rb(int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	osEventFlagsSet(System_StatusHandle, APP_NEED_USART);
	if (xym_port_init() != XYM_OK) {
		logPrintln("X/Y modem port init failed");
		osEventFlagsClear(System_StatusHandle, APP_NEED_USART);
		return;
	}
	ymodem_init(&port->session);

	FIL *fp = NULL;
	xym_sta_t sta = XYM_OK;
	uint16_t len = 0;
	uint32_t received = 0;
	uint32_t fsize = 0;
	uint32_t filled = 0;
	char fname[XYM_PKT_SIZE_128];
	char last_name[XYM_PKT_SIZE_128] = {0}; /* 记录当前打开的文件名，用于失败清理 */

	while (1) {
		sta = ymodem_receive(&port->session, port->packet_buf, &len);
		if (sta == XYM_FIL_GET) {
			/* 新文件开始（Ymodem 支持多文件，逐文件保存） */
			if (fp) {
				if (received != fsize) {
					xymodem_active_cancel(&port->session);
					sta = XYM_ERROR_INVALID_DATA;
					break;
				}
				if (xym_file_cache_flush(fp, &filled) != FR_OK) {
					xymodem_active_cancel(&port->session);
					sta = XYM_ERROR_HW;
					break;
				}
				if (F_close(&fp) != FR_OK) {
					xymodem_active_cancel(&port->session);
					sta = XYM_ERROR_HW;
					break;
				}
				fp = NULL;
			}
			xym_file_decode(port->packet_buf, fname, &fsize);
			if (F_open(&fp, (const uint8_t *)fname, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
				logPrintln("Fail to create %s", fname);
				xymodem_active_cancel(&port->session);
				sta = XYM_ERROR_HW;
				break;
			}
			snprintf(last_name, sizeof(last_name), "%s", fname);
			received = 0;
			continue;
		}
		if (sta == XYM_OK) {
			/* Ymodem 末包会被填充到 128/1024 字节，只保存文件声明的实际长度。 */
			if (fp) {
				uint32_t remain = (received < fsize) ? (fsize - received) : 0;
				if (remain == 0) {
					xymodem_active_cancel(&port->session);
					sta = XYM_ERROR_INVALID_DATA;
					break;
				}
				uint32_t valid_len = (len < remain) ? len : remain;
				if (xym_file_cache_append(fp, port->packet_buf, valid_len, &filled) != FR_OK) {
					xymodem_active_cancel(&port->session);
					sta = XYM_ERROR_HW;
					break;
				}
				received += valid_len;
			}
			continue;
		}
		break; /* XYM_END 正常结束 / 其它错误或取消 */
	}

	/* 正常结束时，实际写入长度必须与 Ymodem 文件信息包完全一致。 */
	if (sta == XYM_END && fp && received != fsize) sta = XYM_ERROR_INVALID_DATA;
	if (sta == XYM_END && fp && xym_file_cache_flush(fp, &filled) != FR_OK) sta = XYM_ERROR_HW;
	if (fp) {
		if (F_close(&fp) != FR_OK) sta = XYM_ERROR_HW;
	}
	xym_port_deinit();
	osEventFlagsClear(System_StatusHandle, APP_NEED_USART);

	if (sta != XYM_END) {
		if (last_name[0]) xym_file_remove(last_name);
		logPrintln("Ymodem receive failed, code %d", (int)sta);
	}
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
rb, shell_rb, Receive Ymodem);

#endif

/**
 * @brief  硬件 CRC16 校验
 * @param  data 数据
 * @param  cnt 数据长度 (Bytes)
 * @retval 16 位 CRC 结果
 */
uint16_t xymodem_port_crc16(const uint8_t *data, const uint32_t cnt) {
	return (uint16_t)HAL_CRC_Calculate(&hcrc, (const uint32_t *)data, cnt);
}

/**
 * @brief  协议数据发送封装
 * @param  data 数据
 * @param  cnt 数据长度 (Bytes)
 * @param  tick 每字节超时 (ms)
 * @retval enum xym_sta
 */
xym_sta_t xymodem_port_send_data(const uint8_t *data, const uint32_t cnt, const uint32_t tick) {
	SHELL_ASSERT(port, return XYM_ERROR_HW);
	uint32_t sent = 0;
	uint32_t start = osKernelGetTickCount();

	while (sent < cnt) {
		short n = port->shell->write((char *)&data[sent], cnt - sent);
		if (n > 0) {
			sent += n;
			continue;
		}

		if ((uint32_t)(osKernelGetTickCount() - start) >= tick) {
			return XYM_ERROR_TIMEOUT;
		}
		osDelay(1);
	}
	return XYM_OK;
}

/**
 * @brief  协议数据接收封装
 * @param  data 数据缓冲区
 * @param  cnt 期望接收长度 (Bytes)
 * @param  tick 每字节超时 (ms)
 * @retval enum xym_sta
 */
xym_sta_t xymodem_port_recv_data(uint8_t *data, const uint32_t cnt, const uint32_t tick) {
	SHELL_ASSERT(port, return XYM_ERROR_HW);
	uint32_t recved = 0;
	uint32_t start = osKernelGetTickCount();

	while (recved < cnt) {
		uint32_t n = port->shell->read((char *)&data[recved], cnt - recved);
		if (n > 0) {
			recved += n;
			continue;
		}

		if ((uint32_t)(osKernelGetTickCount() - start) >= tick) {
			return XYM_ERROR_TIMEOUT;
		}
		osDelay(1);
	}
	return XYM_OK;
}
