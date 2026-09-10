/**
 * @file audio_tui.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief 播放器界面绘制组件源文件
 */

#include "audio_tui.h"
#if AUDIO_SUPPORT_LRC
#include "audio_lrc.h"
#endif
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
static uint32_t s_total_ms;                       // 总时长ms(tui_init记录, 来自meta->duration_ms)
static uint32_t s_sample_rate;                    // 采样率Hz(tui_init记录, 供元数据区显示)
static audio_time_t s_cur_time;                   // 当前播放时间(tui_update更新, 来自port)
static uint8_t s_bits;                            // 位深(tui_init记录, 有损格式为0)
static uint8_t s_volume;                          // 当前音量0-100(按键调节)
static char s_title[AUDIO_META_TAG_LEN];          // 歌名(tui_init记录, 无标签为空)
static char s_artist[AUDIO_META_TAG_LEN];         // 艺术家(tui_init记录, 无标签为空)
static char s_album[AUDIO_META_TAG_LEN];          // 专辑(tui_init记录, 无标签为空)
#if AUDIO_SUPPORT_LRC
static audio_time_t s_lrc_time;    // 上次渲染的歌词行时间(检测变化决定是否重绘歌词区)
static bool s_lrc_has = false;     // 上次渲染是否有歌词行(区分无歌词占位)
#endif

/**
 * @brief 终端原始写函数
 * @param s 字符串
 */
static void tui_write(const char *s) {
    shell.write((char*)s, (uint32_t)strlen(s));
}

/**
 * @brief 判断Unicode码点是否为东亚宽字符(终端占2列)
 *        覆盖CJK统一汉字/扩展、日文假名、谚文及全角符号等常用区段
 * @param cp Unicode码点
 * @return true宽字符(2列), false普通字符(1列)
 */
static bool tui_cp_is_wide(uint32_t cp) {
	// 谚文Jamo
	if (cp >= 0x1100 && cp <= 0x115F) return true;
	// CJK部首/笔画/符号与标点
	if (cp >= 0x2E80 && cp <= 0x303E) return true;
	// 日文假名/片假名及CJK兼容符号
	if (cp >= 0x3041 && cp <= 0x33FF) return true;
	// CJK扩展A / CJK统一汉字(中文/日文汉字主体)
	if (cp >= 0x3400 && cp <= 0x4DBF) return true;
	if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
	// 彝文音节/谚文音节
	if (cp >= 0xA000 && cp <= 0xA4CF) return true;
	if (cp >= 0xAC00 && cp <= 0xD7A3) return true;
	// CJK兼容汉字/竖排与兼容标点
	if (cp >= 0xF900 && cp <= 0xFAFF) return true;
	if (cp >= 0xFE30 && cp <= 0xFE4F) return true;
	// 全角ASCII变体/全角货币符等
	if (cp >= 0xFF00 && cp <= 0xFF60) return true;
	if (cp >= 0xFFE0 && cp <= 0xFFE6) return true;
	// CJK扩展B及之后
	if (cp >= 0x20000 && cp <= 0x2FFFD) return true;
	return false;
}

/**
 * @brief 解析当前UTF-8码点
 * @param p 指向码点起始字节
 * @param plen 输出码点字节数(1-4)
 * @return 码点值
 */
static uint32_t tui_cp_decode(const uint8_t *p, uint32_t *plen) {
	uint32_t cp;
	uint32_t n;
	uint32_t i;

	if (*p < 0x80) {
		cp = *p; n = 1;
	} else if ((*p & 0xE0) == 0xC0) {
		cp = *p & 0x1Fu; n = 2;
	} else if ((*p & 0xF0) == 0xE0) {
		cp = *p & 0x0Fu; n = 3;
	} else {
		cp = *p & 0x07u; n = 4;
	}
	for (i = 1; i < n; i++) cp = (cp << 6) | (p[i] & 0x3Fu);
	*plen = n;
	return cp;
}

/**
 * @brief 生成一行文本并填充空格至界面宽度(无边框, 供底色铺满)
 *        内容按终端列宽截断(宽字符占2列, 不会切断半个UTF-8码点)
 * @param out 输出缓冲(≥AUDIO_TUI_W*3+1)
 * @param text 文本内容(按UTF-8码点逐字拷贝)
 * @param center 是否水平居中(否则左对齐缩进2列)
 */
static void tui_row(uint8_t *out, const uint8_t *text, bool center) {
    uint32_t avail;                 // 内容区可用最大列数
	uint32_t used;                  // 截断后实际占用列数
	uint32_t lead;                  // 行首填充列数
	uint32_t tail;                  // 行尾填充列数
	uint32_t i;
	const uint8_t* p = (const uint8_t*)text;
	char* o = (char *)out;

	// 内容可用宽度: 居中整行可用, 左对齐需留2列缩进
	avail = center ? AUDIO_TUI_WIDTH : (AUDIO_TUI_WIDTH - 2);

	// 第一遍: 统计截断到avail后的实际列数(遇放不下的字符停止)
	used = 0;
	while (*p != '\0') {
		uint32_t n;
		uint32_t cp = tui_cp_decode(p, &n);
		uint32_t col = tui_cp_is_wide(cp) ? 2 : 1;
		if (used + col > avail) break;
		used += col;
		p += n;
	}

	lead = center ? (AUDIO_TUI_WIDTH - used) / 2 : 2;
	tail = AUDIO_TUI_WIDTH - lead - used;

	for (i = 0; i < lead; i++) *o++ = ' ';
	// 第二遍: 逐码点拷贝到used列为止
	p = (const uint8_t*)text;
	while (used > 0) {
		uint32_t n;
		uint32_t cp = tui_cp_decode(p, &n);
		uint32_t col = tui_cp_is_wide(cp) ? 2 : 1;
		memcpy(o, p, n);
		o += n;
		p += n;
		used -= col;
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
static void tui_paint(uint8_t *out, const uint8_t *sgr, const uint8_t *text, bool center, bool newline) {
	uint32_t pos = 0;
	uint32_t len;

	// 先以默认底色擦除整行
	memcpy(out + pos, "\033[2K", 4); pos += 4;
	len = (uint32_t)strlen((const char *)sgr);
	memcpy(out + pos, sgr, len); pos += len;
	tui_row(out + pos, text, center);
	pos += (uint32_t)strlen((char *)out + pos);
	memcpy(out + pos, TUI_COLOR_RESET, 4); pos += 4;
	if (newline) {
		out[pos++] = '\r';
		out[pos++] = '\n';
	}
	shell.write((char *)out, pos);
}

/**
 * @brief 生成微调水平条
 * @param buf 输出缓冲
 * @param cells 总格数
 * @param units 已填充的1/8单元总数
 */
static void tui_bar(uint8_t* buf, uint32_t cells, uint32_t units) {
	/* 1/8..7/8档位字形(八进制转义: 规避armcc按ANSI解析UTF-8出错) */
	static const uint8_t* const g[7] = {
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
    uint8_t *p = buf;

    for (i = 0; i < cells; i++) {
		in = (int32_t)units - (int32_t)(i * 8);   // 当前格已占用的1/8单元数
		if (in <= 0) {
			*p++ = ' ';                            // 未播放部分留空
		} else {
			if (in > 8) in = 8;
            memcpy((char *)p, (in == 8) ? TUI_GLYPH_FULL : (char *)g[in - 1], 3);
            p += 3;
		}
	}
	*p = '\0';
}

/**
 * @brief 时间换算为mm:ss
 * @param buf 输出缓冲
 * @param time 时间标签
 */
static void tui_time(uint8_t* buf, audio_time_t time) {
	snprintf((char *)buf, 8, "%02u:%02u", time.base_s / 60, time.base_s % 60);
}

/**
 * @brief 绘制元数据区(共4行): 歌名/歌手/专辑/采样率位深
 *        歌名/歌手/专辑取解析标签(s_title等), 缺失显示占位符;
 *        采样率/位深行显示tui_init记录的元数据, 有损格式(位深为0)仅显示采样率
 */
static void tui_draw_meta(void) {
	uint8_t buf[AUDIO_TUI_WIDTH * 3 + 32];
    uint8_t row[64];
    uint8_t frq[16];
    uint8_t fmt[48];

    // 采样率kHz显示: 整千Hz不显示小数, 否则保留1位小数
	if (s_sample_rate % 1000 == 0) {
        snprintf((char *)frq, sizeof(frq), "%ukHz", s_sample_rate / 1000);
    } else {
        snprintf((char *)frq, sizeof(frq), "%u.%01ukHz", s_sample_rate / 1000, (s_sample_rate % 1000) / 100);
    }
	if (s_bits > 0) {
        snprintf((char *)fmt, sizeof(fmt), "Sample: %s / %ubit", frq, s_bits);
    } else {
        snprintf((char *)fmt, sizeof(fmt), "Sample: %s", frq);
    }

	// 歌名行白底黑字居中, 其余行终端默认颜色
	tui_paint(buf, TUI_COLOR_TITLE,
		(const uint8_t *)((s_title[0] != '\0') ? s_title : AUDIO_TUI_TITLE), true, true);
    snprintf((char *)row, sizeof(row), "Artist: %s",
		(s_artist[0] != '\0') ? s_artist : AUDIO_TUI_ARTIST);
	tui_paint(buf, TUI_COLOR_RESET, row, false, true);
    snprintf((char *)row, sizeof(row), "Album: %s",
		(s_album[0] != '\0') ? s_album : AUDIO_TUI_ALBUM);
	tui_paint(buf, TUI_COLOR_RESET, row, false, true);
	tui_paint(buf, TUI_COLOR_RESET, fmt, false, true);
}

#if AUDIO_SUPPORT_LRC
/**
 * @brief 绘制歌词区
 * @param line 当前歌词行(Lrc_getlen 返回值, 可NULL)
 */
static void tui_draw_lyric(const lrc_t *line) {
    uint8_t buf[AUDIO_TUI_WIDTH * 3 + 32];
    uint32_t i;

	for (i = 0; i < 2; i++) {
		if (i == 0) {
			// 第一行: 原词(无歌词显示占位符)
			tui_paint(buf, TUI_COLOR_RESET,
				(line != NULL && line->raw != NULL) ? line->raw : AUDIO_TUI_LRC,
				true, true);
		} else if (i == 1) {
			// 第二行: 翻译(无翻译留空)
			tui_paint(buf, TUI_COLOR_RESET,
				(line != NULL && line->tras != NULL) ? line->tras : "",
				true, true);
		}
	}
}

/**
 * @brief 判断歌词行是否与上次渲染不同(Lrc_getlen返回静态地址, 须按时间比较)
 * @param cur 当前歌词行(Lrc_getlen返回值)
 * @return true表示歌词行已变化(需重绘歌词区)
 */
static bool tui_lrc_changed(const lrc_t *cur) {
	if (cur == NULL) {
		// 由有词变为无词视为变化; 持续无词不重复刷新
		if (s_lrc_has) {
			s_lrc_has = false;
			return true;
		}
		return false;
	}
	// 无词变有词, 或行时间变化时需重绘
	if (!s_lrc_has ||
		cur->time.base_s != s_lrc_time.base_s ||
		cur->time.base_100ms != s_lrc_time.base_100ms) {
		s_lrc_has = true;
		s_lrc_time = cur->time;
		return true;
	}
	return false;
}
#endif

/**
 * @brief 绘制控制区
 */
static void tui_draw_control(void) {
	uint8_t buf[AUDIO_TUI_WIDTH * 3 + 32];
    uint8_t bar[3 * AUDIO_PROG_BAR_LEN + 1];
    uint8_t cur[8];
    uint8_t tot[8];
    uint8_t prog[3 * AUDIO_PROG_BAR_LEN + 32];
    uint8_t volt[3 * AUDIO_VOL_BAR_LEN + 32];
    uint32_t units;
	uint32_t cur_ms;

	// 已播放/总时长(总时长未知时进度清零)
	tui_time(cur, s_cur_time);
	cur_ms = (uint32_t)s_cur_time.base_s * 1000 + (uint32_t)s_cur_time.base_100ms * 10;
	if (s_total_ms > 0) {
		snprintf((char *)tot, 8, "%02u:%02u", s_total_ms / 60000, (s_total_ms / 1000) % 60);
		units = (uint32_t)((uint64_t)cur_ms * (AUDIO_PROG_BAR_LEN * 8u) / s_total_ms);
	} else {
        strcpy((char *)tot, "--:--");
        units = 0;
	}

	// 构造进度/音量两行文本(首帧与增量刷新共用)
	tui_bar(bar, AUDIO_PROG_BAR_LEN, units);
    snprintf((char *)prog, sizeof(prog), TUI_GLYPH_PLAY " [%s]  %s / %s", bar, cur, tot);
    tui_bar(bar, AUDIO_VOL_BAR_LEN, (uint32_t)s_volume * (AUDIO_VOL_BAR_LEN * 8u) / 100);
    snprintf((char *)volt, sizeof(volt), "Volume: [%s]  %u%%", bar, s_volume);

    // 进度行换行, 音量行为末行不换行, 供下一帧回跳覆盖
	tui_paint(buf, TUI_COLOR_RESET, prog, false, true);
	tui_paint(buf, TUI_COLOR_RESET, volt, false, false);
}

/**
 * @brief 初始化播放器界面(播放开始前调用一次)
 *        复位首帧标志并记录元数据(采样率/位深/总帧数/音量),
 *        随后输出元数据区; 歌词区与控制区由后续tui_update补齐
 * @param meta 音频元数据(采样率/位深/总帧数)
 * @param volume 当前音量(0-100)
 */
void tui_init(const audio_meta_t* meta, uint8_t volume) {
	s_sample_rate = meta->sample_rate;
	s_bits = meta->bits_per_sample;
	s_total_ms = meta->duration_ms;
	s_cur_time.base_s = 0;
	s_cur_time.base_100ms = 0;
	s_volume = volume;
	tui_active = false;   // 复位首帧标志, 首帧由tui_update补齐歌词区与控制区
	tui_meta = true;      // 元数据区已输出, 异常退出时tui_clear据此清理

	// 记录标签(meta生命周期随播放结束释放, 此处拷贝副本)
	snprintf(s_title, sizeof(s_title), "%s", meta->title);
	snprintf(s_artist, sizeof(s_artist), "%s", meta->artist);
	snprintf(s_album, sizeof(s_album), "%s", meta->album);

	tui_draw_meta();
}

/**
 * @brief 周期性刷新播放器界面(播放循环定时调用)
 *        首帧补齐歌词区与控制区(元数据区已由tui_init输出),
 *        之后增量刷新: 歌词行变化时回退到歌词区起点重绘歌词区+控制区,
 *        否则仅回退1行重绘控制区(进度/音量)
 * @param time 当前播放时间(由port统一维护)
 * @param volume 当前音量(0-100)
 */
void tui_update(const audio_time_t* time, uint8_t volume) {
	s_cur_time = *time;
	s_volume = volume;

	if (tui_active) {
#if AUDIO_SUPPORT_LRC
		// 检查歌词行是否变化: 变化则回退到歌词区起点重绘歌词区+控制区
		const lrc_t *cur = Lrc_getlen(s_cur_time);
		if (tui_lrc_changed(cur)) {
			// 光标在音量行行尾, 上移 歌词区N行+进度行1行(=N+1) 到歌词区第一行行首
			char seq[16];
			snprintf(seq, sizeof(seq), "\033[%uA\r", 3);
			tui_write(seq);
			// 画歌词区(结束时光标在进度行行首)
			tui_draw_lyric(cur);
			// 画控制区(光标停在音量行行尾)
			tui_draw_control();
			return;
		}
#endif
		// 增量刷新: 光标在音量行行尾, 回退1行到进度行行首
		tui_write("\033[1A\r");
		tui_draw_control();
		return;
	}

	// 首帧: 元数据区已由tui_init绘制, 光标在元数据区末行行尾, 直接画歌词区+控制区
#if AUDIO_SUPPORT_LRC
	const lrc_t *cur = Lrc_getlen(s_cur_time);
	s_lrc_has = (cur != NULL);
	if (cur != NULL) s_lrc_time = cur->time;
	tui_draw_lyric(cur);
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
	rows = 8u;
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
