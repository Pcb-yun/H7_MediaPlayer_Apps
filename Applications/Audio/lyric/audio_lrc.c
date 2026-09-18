/**
 * @file audio_lrc.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief 歌词解析组件源文件
 */

#include "audio_lrc.h"
#if AUDIO_SUPPORT_LRC
#include "audio_port.h"
#include "FreeRTOS.h"
#include <string.h>


static lrcdata_t g_lrcdata;
static lrc_t cur_line;        // 当前返回的歌词行
static uint32_t last_ms = 0;  // 上次查询时间(ms), 检测进度回退
static uint8_t s_raw_buf[256]; // 当前原词文本(提取后, 跳过逐字时间戳)
static uint8_t s_tras_buf[256]; // 当前翻译文本(提取后)


/**
 * @brief 从 ptr 位置解析一行歌词
 * @param buf 歌词数据
 * @param size 数据大小
 * @param ptr 当前解析位置
 * @return 下一行起始位置(解析失败返回 ptr 不变)
 */
static __attribute__((section(".ITCM")))
uint32_t parse_line(uint8_t *buf, uint32_t size, uint32_t ptr) {
    uint16_t mm = 0, ss = 0;
	uint8_t xx = 0;
	uint16_t rlen = 0, tlen = 0;

	// 跳过行首空白和换行
	while (ptr < size && (buf[ptr] == '\r' || buf[ptr] == '\n' || buf[ptr] == ' ')) ptr++;
	if (ptr >= size) return ptr;

	// 解析时间标签 [mm:ss.xx]
	if (buf[ptr] == '[') {
		ptr++;
		while (ptr < size && buf[ptr] >= '0' && buf[ptr] <= '9') {
			mm = mm * 10 + (buf[ptr] - '0'); ptr++;
		}
		if (ptr < size && buf[ptr] == ':') ptr++;
		while (ptr < size && buf[ptr] >= '0' && buf[ptr] <= '9') {
			ss = ss * 10 + (buf[ptr] - '0'); ptr++;
		}
		if (ptr < size && buf[ptr] == '.') ptr++;
		while (ptr < size && buf[ptr] >= '0' && buf[ptr] <= '9') {
			xx = xx * 10 + (buf[ptr] - '0'); ptr++;
		}
		if (ptr < size && buf[ptr] == ']') ptr++;
	}
	cur_line.time.base_s = mm * 60 + ss;
	cur_line.time.base_100ms = xx;

	// 提取原词到 s_raw_buf, 跳过 <...> 逐字时间戳
	while (ptr < size && buf[ptr] != '\r' && buf[ptr] != '\n' && rlen < 255) {
		if (buf[ptr] == '<') {
			while (ptr < size && buf[ptr] != '>') ptr++;
			if (ptr < size) ptr++;
		} else {
			s_raw_buf[rlen++] = buf[ptr++];
		}
	}
	s_raw_buf[rlen] = '\0';
	cur_line.raw = s_raw_buf;

	// 跳过原词行换行
	while (ptr < size && (buf[ptr] == '\r' || buf[ptr] == '\n')) ptr++;

	// 尝试解析翻译行(下一行时间标签相同视为翻译)
	cur_line.tras = NULL;
	if (ptr < size && buf[ptr] == '[') {
		uint32_t save_ptr = ptr;
		uint16_t tmm = 0, tss = 0;
		uint8_t txx = 0;
		ptr++;
		while (ptr < size && buf[ptr] >= '0' && buf[ptr] <= '9') {
			tmm = tmm * 10 + (buf[ptr] - '0'); ptr++;
		}
		if (ptr < size && buf[ptr] == ':') ptr++;
		while (ptr < size && buf[ptr] >= '0' && buf[ptr] <= '9') {
			tss = tss * 10 + (buf[ptr] - '0'); ptr++;
		}
		if (ptr < size && buf[ptr] == '.') ptr++;
		while (ptr < size && buf[ptr] >= '0' && buf[ptr] <= '9') {
			txx = txx * 10 + (buf[ptr] - '0'); ptr++;
		}
		if (ptr < size && buf[ptr] == ']') ptr++;
		// 时间相同视为翻译行
		if (tmm == mm && tss == ss && txx == xx) {
			while (ptr < size && buf[ptr] != '\r' && buf[ptr] != '\n' && tlen < 255) {
				s_tras_buf[tlen++] = buf[ptr++];
			}
			s_tras_buf[tlen] = '\0';
			cur_line.tras = s_tras_buf;
			while (ptr < size && (buf[ptr] == '\r' || buf[ptr] == '\n')) ptr++;
		} else {
			ptr = save_ptr; // 时间不同, 不是翻译行, 回退
		}
	}

	return ptr;
}

/**
 * @brief 按当前播放时间查询对应歌词行
 * @param time 当前时间
 * @return 歌词行指针(未初始化返回NULL)
 */
const __attribute__((section(".ITCM")))
lrc_t *Lrc_getlen(audio_time_t time) {
    uint32_t cur_ms = (uint32_t)time.base_s * 1000 + (uint32_t)time.base_100ms * 10;
	uint8_t *buf = g_lrcdata.lrc_data;
	uint32_t size = g_lrcdata.size;
	uint32_t ptr;
	uint32_t target_ptr = 0;
	bool has_target = false;

	if (buf == NULL || size == 0) return NULL;

	// 进度回退: 重置从头查找
	if (cur_ms < last_ms) {
		g_lrcdata.curent_ptr = 0;
	}
	last_ms = cur_ms;
	ptr = g_lrcdata.curent_ptr;

	// 从 curent_ptr 往后找时间 <= cur_ms 的最后一行
	while (ptr < size) {
		uint32_t line_start = ptr;
		uint32_t next_ptr = parse_line(buf, size, ptr);
		if (next_ptr <= line_start) break; // 没有进展

		uint32_t line_ms = (uint32_t)cur_line.time.base_s * 1000 +
			(uint32_t)cur_line.time.base_100ms * 10;

		if (line_ms <= cur_ms) {
			target_ptr = line_start;
			has_target = true;
			ptr = next_ptr;
		} else {
			break; // 这行时间 > cur_ms, 停止
		}
	}

	if (has_target) {
		parse_line(buf, size, target_ptr); // 重新解析目标行填充 cur_line
		g_lrcdata.curent_ptr = target_ptr;   // 指向目标行, 下次从这里继续
		return &cur_line;
	}

	// 没找到时间 <= cur_ms 的行(前奏期间), 返回首行
	if (size > 0) {
		parse_line(buf, size, 0);
		return &cur_line;
	}
	return NULL;
}

/**
 * @brief 释放歌词模块内部资源
 */
void Lrc_free(void) {
	if (g_lrcdata.lrc_data != NULL) {
		vPortFree(g_lrcdata.lrc_data);
		g_lrcdata.lrc_data = NULL;
	}

	g_lrcdata.curent_ptr = 0;
	g_lrcdata.size = 0;
	last_ms = 0;
}

/**
 * @brief 注册歌词数据
 * @param data pvPortMalloc分配、以'\0'结尾的歌词文本
 * @param size 歌词文本长度(不含结尾'\0')
 */
void Lrc_register(uint8_t *data, uint32_t size) {
	Lrc_free();   // 清旧数据
	if (data == NULL || size == 0) return;

	g_lrcdata.lrc_data = data;
	g_lrcdata.curent_ptr = 0;
	g_lrcdata.size = size;
	last_ms = 0;
}

#endif /* AUDIO_SUPPORT_LRC */
