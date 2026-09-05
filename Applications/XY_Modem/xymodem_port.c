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


/* XYmodem 端口上下文 */
struct XYM_Port_t {
	Shell *shell;
	xym_session_t session;
	uint8_t *buffer;		// 收发缓冲
	uint32_t buffer_size;	// 缓冲大小
};
static struct XYM_Port_t *port = NULL;


static xym_sta_t xymodem_port_send_data(const uint8_t *data, const uint32_t cnt, const uint32_t tick);
static xym_sta_t xymodem_port_recv_data(uint8_t *data, const uint32_t cnt, const uint32_t tick);
uint16_t xymodem_port_crc16(const uint8_t *data, const uint32_t cnt);

/**
 * @brief  取路径中的文件名（去除目录前缀）
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
 * @brief  编码 Ymodem 文件信息包：文件名\0文件大小
 * @param  buff 数据缓冲（128 Bytes）
 * @param  name 文件名
 * @param  size 文件大小 (Bytes)
 */
static void xym_file_encode(uint8_t *buff, const char *name, uint32_t size) {
	uint16_t i = 0;
	for (i = 0; name[i] && i < XYM_PKT_SIZE_128 - 16; ++i) {
		buff[i] = (uint8_t)name[i];
	}
	buff[i++] = '\0';
	snprintf((char *)&buff[i], XYM_PKT_SIZE_128 - i, "%lu", (unsigned long)size);
}

/**
 * @brief  解码 Ymodem 文件信息包：文件名\0文件大小
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
 * @param  path 文件路径（utf8，相对路径基于 FatFS 当前目录）
 */
static void xym_file_remove(const char *path) {
	TCHAR wpath[512];
	utf8to16((const uint8_t *)path, wpath, sizeof(wpath) / sizeof(TCHAR));
	f_unlink(wpath);
}

/**
 * @brief  将累积缓冲中的数据一次性写入文件
 * @param  fp  文件句柄
 * @param  buf 累积缓冲
 * @param  cnt 待写入字节数
 * @retval FR_OK 成功，否则失败
 */
static FRESULT xym_buf_flush(FIL *fp, const uint8_t *buf, uint32_t cnt) {
	UINT bw = 0;
	if (cnt == 0) return FR_OK;
	if (f_write(fp, buf, cnt, &bw) != FR_OK || bw != cnt) return FR_INT_ERR;
	return FR_OK;
}

/**
 * @brief  端口初始化
 * @retval enum xym_sta
 */
static xym_sta_t xym_port_init(void) {
	SHELL_ASSERT(shellGetCurrent(), return XYM_ERROR_HW);
	if (port == NULL) {
		port = pvPortMalloc(sizeof(struct XYM_Port_t));
		SHELL_ASSERT(port, return XYM_ERROR_HW);
	}
	memset(port, 0, sizeof(struct XYM_Port_t));
	port->shell = shellGetCurrent();

	// 按空闲堆动态分配收发缓冲(512整数倍), 为系统保留XYMODEM_RESERVED_MEM
	size_t free = xPortGetFreeHeapSize();
	if (free <= XYMODEM_RESERVED_MEM) {
		logPrintln("no enough memory for xymodem buffer");
		vPortFree(port);
		port = NULL;
		return XYM_ERROR_HW;
	}
	port->buffer_size = (uint32_t)((free - XYMODEM_RESERVED_MEM) / 512u * 512u);
	port->buffer = pvPortMalloc(port->buffer_size);
	if (port->buffer == NULL) {
		logPrintln("buffer alloc failed");
		vPortFree(port);
		port = NULL;
		return XYM_ERROR_HW;
	}

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
		vPortFree(port->buffer);
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
	if (!FS_Check()) { logPrintln("File system is not mounted"); return; }

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
	UINT br = 0;

	while (1) {
		if (sent < fsize) {
			len = ((fsize - sent) > XYM_PKT_SIZE_1024) ? XYM_PKT_SIZE_1024 : (uint16_t)(fsize - sent);
			if (f_read(fp, port->buffer, len, &br) != FR_OK) { sta = XYM_ERROR_HW; break; }
			len = (uint16_t)br;
			sta = xmodem_transmit(&port->session, port->buffer, len);
			if (sta != XYM_OK) break;
			sent += len;
		} else {
			sta = xmodem_transmit(&port->session, port->buffer, 0);
			break;
		}
	}

	F_close(&fp);
	xym_port_deinit();
	osEventFlagsClear(System_StatusHandle, APP_NEED_USART);

	if (sta == XYM_END) logPrintln("Xmodem send success, %lu bytes", (unsigned long)fsize);
	else logPrintln("Xmodem send failed, code %d", (int)sta);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
sx, shell_sx, Send Xmodem);

/**
 * @brief  X协议接收
 */
static void shell_rx(int argc, char *argv[]) {
	if (argc < 2) { logPrintln("Usage: rx <file>"); return; }
	if (!FS_Check()) { logPrintln("File system is not mounted"); return; }

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
	uint32_t received = 0;
	uint32_t filled = 0;         /* 累积缓冲中待写入文件的字节数 */

	if (F_open(&fp, (const uint8_t *)argv[1], FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
		logPrintln("Fail to create %s", argv[1]);
		xym_port_deinit();
		osEventFlagsClear(System_StatusHandle, APP_NEED_USART);
		return;
	}

	while (1) {
		sta = xmodem_receive(&port->session, port->buffer, &len);
		if (sta == XYM_OK) {
			/* 数据包累积到缓冲，写满则一次性写入文件 */
			received += len;
			if (filled > 0 && filled + len > port->buffer_size) {
				if (xym_buf_flush(fp, port->buffer, filled) != FR_OK) {
					xymodem_active_cancel(&port->session);
					sta = XYM_ERROR_HW;
					break;
				}
				filled = 0;
			}
			memmove(&port->buffer[filled], port->buffer, len);
			filled += len;
			continue;
		}
		break; /* XYM_END 正常结束 / 其它错误 */
	}

	/* 收尾：写入残留数据并关闭文件 */
	if (xym_buf_flush(fp, port->buffer, filled) != FR_OK) sta = XYM_ERROR_HW;
	F_close(&fp);
	if (sta != XYM_END) xym_file_remove(argv[1]);
	xym_port_deinit();
	osEventFlagsClear(System_StatusHandle, APP_NEED_USART);

	if (sta == XYM_END) logPrintln("Xmodem receive success, %lu bytes", (unsigned long)received);
	else logPrintln("Xmodem receive failed, code %d", (int)sta);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
rx, shell_rx, Receive Xmodem);

#endif

#if XYMODEM_USE_YMODEM

/**
 * @brief  Y协议发送
 */
static void shell_sb(int argc, char *argv[]) {
	if (argc < 2) { logPrintln("Usage: sb <file>"); return; }
	if (!FS_Check()) { logPrintln("File system is not mounted"); return; }

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
	ymodem_init(&port->session);

	xym_sta_t sta = XYM_OK;
	uint16_t len = 0;
	uint32_t sent = 0;
	UINT br = 0;
	uint8_t first = 1;

	while (1) {
		if (first) {
			/* 首包：文件信息包（文件名 + 文件大小） */
			memset(port->buffer, 0, XYM_PKT_SIZE_128);
			xym_file_encode(port->buffer, xym_file_basename(argv[1]), fsize);
			sta = ymodem_transmit(&port->session, port->buffer, XYM_PKT_SIZE_128);
			first = 0;
			if (sta != XYM_OK) break;
			continue;
		}
		if (sent < fsize) {
			/* 读文件数据并发送 */
			len = ((fsize - sent) > XYM_PKT_SIZE_1024) ? XYM_PKT_SIZE_1024 : (uint16_t)(fsize - sent);
			if (f_read(fp, port->buffer, len, &br) != FR_OK) { sta = XYM_ERROR_HW; break; }
			len = (uint16_t)br;
			sta = ymodem_transmit(&port->session, port->buffer, len);
			if (sta != XYM_OK) break;
			sent += len;
		} else {
			/* 数据发完：EOT 结束当前文件 */
			sta = ymodem_transmit(&port->session, port->buffer, 0);
			if (sta == XYM_FIL_SET) {
				/* 再发空文件信息包，结束整个会话 */
				memset(port->buffer, 0, XYM_PKT_SIZE_128);
				sta = ymodem_transmit(&port->session, port->buffer, 0);
			}
			break;
		}
	}

	F_close(&fp);
	xym_port_deinit();
	osEventFlagsClear(System_StatusHandle, APP_NEED_USART);

	if (sta == XYM_END) logPrintln("Ymodem send success, %lu bytes", (unsigned long)fsize);
	else logPrintln("Ymodem send failed, code %d", (int)sta);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
sb, shell_sb, Send Ymodem);

/**
 * @brief  Y协议接收
 */
static void shell_rb(int argc, char *argv[]) {
	if (!FS_Check()) { logPrintln("File system is not mounted"); return; }

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
	uint32_t filled = 0;         /* 累积缓冲中待写入文件的字节数 */
	char fname[XYM_PKT_SIZE_128];
	char last_name[XYM_PKT_SIZE_128] = {0}; /* 记录当前打开的文件名，用于失败清理 */

	while (1) {
		sta = ymodem_receive(&port->session, port->buffer, &len);
		if (sta == XYM_FIL_GET) {
			/* 新文件开始（Ymodem 支持多文件，逐文件保存） */
			if (fp) {
				if (xym_buf_flush(fp, port->buffer, filled) != FR_OK) {
					xymodem_active_cancel(&port->session);
					sta = XYM_ERROR_HW;
					break;
				}
				filled = 0;
				F_close(&fp);
				fp = NULL;
			}
			xym_file_decode(port->buffer, fname, &fsize);
			if (argc >= 2) snprintf(fname, sizeof(fname), "%s", argv[1]); /* 用户指定保存文件名 */
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
			/* 数据包累积到缓冲，写满则一次性写入文件 */
			received += len;
			if (filled > 0 && filled + len > port->buffer_size) {
				if (fp && xym_buf_flush(fp, port->buffer, filled) != FR_OK) {
					xymodem_active_cancel(&port->session);
					sta = XYM_ERROR_HW;
					break;
				}
				filled = 0;
			}
			if (fp) {
				memmove(&port->buffer[filled], port->buffer, len);
				filled += len;
			}
			continue;
		}
		break; /* XYM_END 正常结束 / 其它错误或取消 */
	}

	/* 收尾：写入残留数据并关闭文件 */
	if (fp) {
		if (xym_buf_flush(fp, port->buffer, filled) != FR_OK) sta = XYM_ERROR_HW;
		F_close(&fp);
	}
	if (sta != XYM_END && last_name[0]) xym_file_remove(last_name);
	xym_port_deinit();
	osEventFlagsClear(System_StatusHandle, APP_NEED_USART);

	if (sta == XYM_END) logPrintln("Ymodem receive success, %lu bytes", (unsigned long)received);
	else logPrintln("Ymodem receive failed, code %d", (int)sta);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
rb, shell_rb, Receive Ymodem);

#endif

/**
 * @brief  硬件 CRC16 校验（CRC-16/XMODEM：poly 0x1021, init 0, 无反转）
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

	while (sent < cnt) {
		uint32_t start = osKernelGetTickCount();
		short n = port->shell->write((char *)&data[sent], cnt - sent);
		if (n <= 0) {
			/* 一字节都未发出，按单字节超时处理 */
			if ((uint32_t)(osKernelGetTickCount() - start) >= tick) {
				return XYM_ERROR_TIMEOUT;
			}
			continue;
		}
		sent += n;
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
	uint32_t n = 0;

	while (recved < cnt) {
		uint32_t start = osKernelGetTickCount();
		for (;;) {
			n = port->shell->read((char *)&data[recved], cnt - recved);
			if (n > 0) {
				recved += n;
				break;
			}
			/* 本次未读到任何字节，按单字节超时判断 */
			if ((uint32_t)(osKernelGetTickCount() - start) >= tick) {
				return XYM_ERROR_TIMEOUT;
			}
		}
	}
	return XYM_OK;
}
