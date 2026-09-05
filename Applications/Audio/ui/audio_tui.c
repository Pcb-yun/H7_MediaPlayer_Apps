/**
 * @file audio_tui.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief 播放器界面绘制组件源文件
 */

#include "audio_tui.h"
#include "shell.h"
#include "shell_port.h"
#include <string.h>
#include <stdio.h>

/* ---------- 播放器TUI界面参数 ---------- */
#define AUDIO_TUI_TITLE  "<unknown>"              // 歌名占位(标签解析未实现)
#define AUDIO_TUI_ARTIST "<unknown>"              // 艺术家占位(标签解析未实现)
#define AUDIO_TUI_ALBUM  "<unknown>"              // 专辑占位(标签解析未实现)
#define AUDIO_TUI_LRC    "<no lyrics>"            // 歌词占位(歌词同步未实现)

/* Unicode图标以八进制转义写入: 避免armcc按ANSI(GBK)解析UTF-8字面量出错 */
#define TUI_GLYPH_PLAY  "\342\226\266"   // 播放中图标
#define TUI_GLYPH_FULL  "\342\226\210"   // 进度整格填充块

/* 区域配色(ANSI SGR): 标题行白底, 其余行恢复终端默认颜色 */
#define TUI_COLOR_TITLE  "\033[47;30m"      // 标题区: 白底黑字
#define TUI_COLOR_RESET  "\033[0m"           // 属性重置(终端默认颜色)

static bool tui_active = false;                   // 控制区(面板底部)是否已绘制, 光标停在音量行行尾
static bool tui_meta = false;                     // 元数据区是否已输出(tui_init执行过)
static uint64_t s_total_frames;                   // 总PCM帧数(tui_init记录)
static uint32_t s_sample_rate;                    // 采样率Hz(tui_init记录)
static uint32_t s_play_frames;                    // 已播放PCM帧数(tui_update更新)
static uint8_t s_bits;                            // 位深(tui_init记录, 有损格式为0)
static uint8_t s_volume;                          // 当前音量0-100(按键调节)

/**
 * @brief 终端原始写函数
 * @param s 字符串
 */
static void tui_write(const char* s) {
	shell.write((char*)s, (uint32_t)strlen(s));
}

/**
 * @brief 计算字符串在终端中的显示宽度
 *        界面仅使用ASCII与3字节单宽图标等字符, 每个UTF-8码点按1列计
 * @param s 字符串
 * @return 显示宽度(列数)
 */
static uint32_t tui_width(const char* s) {
	uint32_t w = 0;
	const uint8_t* p = (const uint8_t*)s;

	while (*p != '\0') {
		if (*p < 0x80) {
			p++;
		} else if ((*p & 0xE0) == 0xC0) {
			p += 2;
		} else if ((*p & 0xF0) == 0xE0) {
			p += 3;
		} else {
			p += 4;
		}
		w++;
	}
	return w;
}

/**
 * @brief 生成一行文本并填充空格至界面宽度(无边框, 供底色铺满)
 * @param out 输出缓冲(≥AUDIO_TUI_W*3+1)
 * @param text 文本内容(按UTF-8码点逐字拷贝)
 * @param center 是否水平居中(否则左对齐缩进2列)
 */
static void tui_row(char* out, const char* text, bool center) {
	uint32_t cw = tui_width(text);
	uint32_t lead;
	uint32_t tail;
	uint32_t i;
	const uint8_t* p = (const uint8_t*)text;
	char* o = out;

	// 先按可用宽度截断再计算tail, 避免缩进占用后tail下溢为巨大值
	if (center) {
		// 居中: 无固定缩进, 内容最多占满整行
		if (cw > AUDIO_TUI_WIDTH) cw = AUDIO_TUI_WIDTH;
		lead = (AUDIO_TUI_WIDTH - cw) / 2;
	} else {
		// 左对齐: 固定缩进2列, 内容可用宽度为W-2
		if (cw > AUDIO_TUI_WIDTH - 2) cw = AUDIO_TUI_WIDTH - 2;
		lead = 2;
	}
	tail = AUDIO_TUI_WIDTH - lead - cw;

	for (i = 0; i < lead; i++) *o++ = ' ';
	for (i = 0; i < cw; i++) {
		uint8_t c = *p;
		uint32_t n = 1;
		if (c >= 0x80) {
			if ((c & 0xE0) == 0xC0) n = 2;
			else if ((c & 0xF0) == 0xE0) n = 3;
			else if ((c & 0xF8) == 0xF0) n = 4;
		}
		memcpy(o, p, n);
		o += n;
		p += n;
	}
	for (i = 0; i < tail; i++) *o++ = ' ';
	*o = '\0';
}

/**
 * @brief 以指定背景色输出一行(先擦除旧行, 行尾重置属性)
 * @param out 行缓冲(≥AUDIO_TUI_W*3+32)
 * @param sgr ANSI前景/背景设置
 * @param text 行内容
 * @param center 是否水平居中
 * @param newline 行尾是否换行
 */
static void tui_paint(char* out, const char* sgr, const char* text, bool center, bool newline) {
	tui_row(out, text, center);
	tui_write("\033[2K");   // 先以默认底色擦除整行, 防止上色后再擦除使背景蔓延到行尾
	tui_write(sgr);
	tui_write(out);
	tui_write(TUI_COLOR_RESET);
	if (newline) tui_write("\r\n");
}

/**
 * @brief 生成微调水平条: 每格细分为8个1/8单元
 *        不足整格时按1/8档位用半格字形平滑过渡, 未填充部分留空
 * @param buf 输出缓冲(≥cells*3+1)
 * @param cells 总格数
 * @param units 已填充的1/8单元总数(每格8单元)
 */
static void tui_bar(char* buf, uint32_t cells, uint32_t units) {
	/* 1/8..7/8档位字形(八进制转义: 规避armcc按ANSI解析UTF-8出错) */
	static const char* const g[7] = {
		"\342\226\217",   // 1/8
		"\342\226\216",   // 2/8
		"\342\226\215",   // 3/8
		"\342\226\214",   // 4/8
		"\342\226\213",   // 5/8
		"\342\226\212",   // 6/8
		"\342\226\211",   // 7/8
	};
	uint32_t i;
	int32_t in;
	char* p = buf;

	for (i = 0; i < cells; i++) {
		in = (int32_t)units - (int32_t)(i * 8);   // 当前格已占用的1/8单元数
		if (in <= 0) {
			*p++ = ' ';                            // 未播放部分留空
		} else {
			if (in > 8) in = 8;
			memcpy(p, (in == 8) ? TUI_GLYPH_FULL : g[in - 1], 3);
			p += 3;
		}
	}
	*p = '\0';
}

/**
 * @brief 帧数换算为mm:ss
 * @param buf 输出缓冲(≥8)
 * @param frames 帧数
 * @param rate 采样率
 */
static void tui_time(char* buf, uint64_t frames, uint32_t rate) {
	uint64_t sec = (rate > 0) ? frames / rate : 0;

	snprintf(buf, 8, "%02u:%02u", (uint32_t)(sec / 60), (uint32_t)(sec % 60));
}

/**
 * @brief 绘制元数据区(共4行): 歌名/歌手/专辑/采样率位深
 *        标签解析未实现, 歌名/歌手/专辑暂显示占位符;
 *        采样率/位深行显示tui_init记录的元数据, 有损格式(位深为0)仅显示采样率
 */
static void tui_draw_meta(void) {
	char buf[AUDIO_TUI_WIDTH * 3 + 32];
	char frq[16];
	char fmt[48];

	// 采样率kHz显示: 整千Hz不显示小数, 否则保留1位小数
	if (s_sample_rate % 1000 == 0) {
		snprintf(frq, sizeof(frq), "%ukHz", s_sample_rate / 1000);
	} else {
		snprintf(frq, sizeof(frq), "%u.%01ukHz", s_sample_rate / 1000, (s_sample_rate % 1000) / 100);
	}
	if (s_bits > 0) {
		snprintf(fmt, sizeof(fmt), "Sample: %s / %ubit", frq, s_bits);
	} else {
		snprintf(fmt, sizeof(fmt), "Sample: %s", frq);
	}

	// 歌名行白底黑字居中, 其余行终端默认颜色
	tui_paint(buf, TUI_COLOR_TITLE, AUDIO_TUI_TITLE, true, true);
	tui_paint(buf, TUI_COLOR_RESET, "Artist: " AUDIO_TUI_ARTIST, false, true);
	tui_paint(buf, TUI_COLOR_RESET, "Album: " AUDIO_TUI_ALBUM, false, true);
	tui_paint(buf, TUI_COLOR_RESET, fmt, false, true);
}

#if AUDIO_SUPPORT_LRC
/**
 * @brief 绘制歌词区(共AUDIO_TUI_LYRIC_ROWS行)
 *        歌词同步未实现: 输出预留行, 中间行居中显示占位符
 */
static void tui_draw_lyric(void) {
	char buf[AUDIO_TUI_WIDTH * 3 + 32];
	uint32_t i;

	for (i = 0; i < AUDIO_TUI_LYRIC_ROWS; i++) {
		tui_paint(buf, TUI_COLOR_RESET, (i == AUDIO_TUI_LYRIC_ROWS / 2) ? AUDIO_TUI_LRC : "",
			(i == AUDIO_TUI_LYRIC_ROWS / 2), true);
	}
}
#endif

/**
 * @brief 绘制控制区: 进度行(播放图标+进度条+已播/总时长)与音量行
 *        非首帧时先将光标回退1行, 覆盖上一帧控制区实现增量刷新
 */
static void tui_draw_control(void) {
	char buf[AUDIO_TUI_WIDTH * 3 + 32];
	char bar[3 * AUDIO_PROG_BAR_LEN + 1];
	char cur[8];
	char tot[8];
	char prog[96];
	char volt[96];
	uint32_t units;

	// 已播放/总时长(总时长未知时进度清零)
	tui_time(cur, s_play_frames, s_sample_rate);
	if (s_total_frames > 0) {
		tui_time(tot, s_total_frames, s_sample_rate);
		units = (uint32_t)((uint64_t)s_play_frames * (AUDIO_PROG_BAR_LEN * 8u) / s_total_frames);
	} else {
		strcpy(tot, "--:--");
		units = 0;
	}

	// 构造进度/音量两行文本(首帧与增量刷新共用)
	tui_bar(bar, AUDIO_PROG_BAR_LEN, units);
	snprintf(prog, sizeof(prog), TUI_GLYPH_PLAY " [%s]  %s / %s", bar, cur, tot);
	tui_bar(bar, AUDIO_VOL_BAR_LEN, (uint32_t)s_volume * (AUDIO_VOL_BAR_LEN * 8u) / 100);
	snprintf(volt, sizeof(volt), "Volume: [%s]  %u%%", bar, s_volume);

	// 增量刷新: 光标位于音量行行尾, 回退1行到进度行后再覆盖重绘
	if (tui_active) {
		tui_write("\033[1A\r");
	}

	// 进度行换行, 音量行为末行不换行, 供下一帧回跳覆盖
	tui_paint(buf, TUI_COLOR_RESET, prog, false, true);
	tui_paint(buf, TUI_COLOR_RESET, volt, false, false);
}

/**
 * @brief 初始化播放器界面(播放开始前调用一次)
 *        复位首帧标志并记录元数据(采样率/位深/总帧数/音量),
 *        随后输出元数据区; 歌词区与控制区由后续tui_update补齐
 * @param path 文件路径(预留: 标签解析未实现, 歌名暂显示占位符)
 * @param meta 音频元数据(采样率/位深/总帧数)
 * @param volume 当前音量(0-100)
 */
void tui_init(const char* path, const audio_meta_t* meta, uint8_t volume) {
	(void)path;   // 预留: 待标签解析/文件名显示实现后再使用

	s_sample_rate = meta->sample_rate;
	s_bits = meta->bits_per_sample;
	s_total_frames = meta->total_frames;
	s_play_frames = 0;
	s_volume = volume;
	tui_active = false;   // 复位首帧标志, 首帧由tui_update补齐歌词区与控制区
	tui_meta = true;      // 元数据区已输出, 异常退出时tui_clear据此清理

	tui_draw_meta();
}

/**
 * @brief 周期性刷新播放器界面(播放循环定时调用)
 *        首帧补齐歌词区与控制区(元数据区已由tui_init输出),
 *        之后仅重绘控制区进度/音量两行; 歌词同步实现前歌词区保持静止
 * @param play_frames 已播放PCM帧数
 * @param volume 当前音量(0-100)
 */
void tui_update(uint32_t play_frames, uint8_t volume) {
	s_play_frames = play_frames;
	s_volume = volume;

	if (tui_active) {
		// 增量刷新: 控制区内完成回退1行重绘
		tui_draw_control();
		return;
	}

	// 首帧: 元数据区已由tui_init绘制, 补齐歌词区与控制区
#if AUDIO_SUPPORT_LRC
	tui_draw_lyric();
#endif
	tui_draw_control();
	tui_active = true;
}

/**
 * @brief 结束播放界面(本次播放结束时调用)
 *        光标位于末行(音量行)行尾, 上移界面总行数回到命令行行,
 *        再清除光标之后(命令行行右侧及以下所有界面行)内容,
 *        由shell换行后在下一行重新输出提示符
 */
void tui_clear(void) {
	char seq[24];
	uint32_t rows;

	// 未输出任何界面行(如初始化前即失败)无需处理
	if (!tui_active && !tui_meta) return;

	// 界面行数 = 元数据区4行 + 歌词区(仅启用时) + 进度/音量2行
#if AUDIO_SUPPORT_LRC
	rows = AUDIO_TUI_LYRIC_ROWS + 6u;
#else
	rows = 6u;
#endif
	// 播放启动失败等仅输出元数据区的情形, 只上移元数据区行数
	if (!tui_active) rows = 4u;

	snprintf(seq, sizeof(seq), "\033[%uA\033[J", rows);
	tui_write(seq);

	tui_active = false;
	tui_meta = false;
}
