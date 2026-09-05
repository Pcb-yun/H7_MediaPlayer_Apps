/**
 * @file audio_port.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief 音频解码播放接口层源文件
 */

#include "audio_port.h"
#include "audio_tui.h"
#include "shell.h"
#include "shell_port.h"
#include "log.h"
#include "sai.h"
#include "shell_cmd_group.h"
#include "Events.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include <string.h>
#include <stdio.h>


#if AUDIO_SUPPORT_WAV
#include "audio_wav.h"
#endif /* AUDIO_SUPPORT_WAV */
#if AUDIO_SUPPORT_MP3
#include "audio_mp3.h"
#endif /* AUDIO_SUPPORT_MP3 */
#if AUDIO_SUPPORT_FLAC
#include "audio_flac.h"
#endif /* AUDIO_SUPPORT_FLAC */
#if AUDIO_SUPPORT_LRC
#include "audio_lrc.h"
#endif /* AUDIO_SUPPORT_LRC */

/**
 * @brief 播放器运行状态
 */
typedef struct {
	const audio_decoder_t* dec;		// 当前解码器接口
	const audio_meta_t* meta;		// 当前音频元数据
	void* dec_ctx;					// 当前解码器句柄
	float* pcm;						// 解码float PCM缓冲(动态)
	uint32_t* tx[2];				// SAI输出缓冲
	uint8_t channels;				// 当前声道数
	uint8_t fill_idx;				// 当前填充半区
	uint32_t frames;				// 每半区PCM帧数(按可用内存动态计算)
	uint32_t pcm_size;				// pcm缓冲元素数
	uint32_t tx_size;				// 单个tx缓冲元素数
	osSemaphoreId_t sem;			// DMA半区空闲信号量
} audio_port_t;

static audio_port_t* g_port = NULL;			// 当前播放器实例
static uint8_t volume = 1;					// 音量(0-100)
static bool audio_set_freq(uint32_t sample_rate);


/**
 * @brief 计算每半区PCM帧数(按空闲堆尽量取大, 为系统保留AUDIO_RESERVED_MEM)
 * @param channels 声道数
 * @return 帧数, 0表示可用内存不足
 */
static uint32_t audio_calc_frames(uint8_t channels) {
	size_t free = xPortGetFreeHeapSize();
	uint32_t frames;

	if (free <= AUDIO_RESERVED_MEM) return 0;
	// 每帧占用内存: pcm(float) + tx双缓冲(uint32) = 3 * channels * 4 字节
	frames = (uint32_t)((free - AUDIO_RESERVED_MEM) / (3u * channels * 4u));
	frames &= ~3u;   // 向下取整到4的倍数, 保持声道对齐
	return frames;
}

/**
 * @brief 按指定帧数分配pcm/tx缓冲
 * @param frames 每半区PCM帧数
 * @return true成功, false内存不足
 */
static bool audio_buf_alloc(uint32_t frames) {
	audio_port_t* port = g_port;

	port->frames = frames;
	port->pcm_size = frames * port->channels;
	port->tx_size = frames * port->channels;

	port->pcm = (float*)pvPortMalloc(port->pcm_size * sizeof(float));
	if (port->pcm == NULL) goto fail;
	port->tx[0] = (uint32_t*)pvPortMalloc(port->tx_size * 2 * sizeof(uint32_t));
	if (port->tx[0] == NULL) goto fail;
	port->tx[1] = port->tx[0] + port->tx_size;
	return true;

fail:
	vPortFree(port->pcm);
	vPortFree(port->tx[0]);
	return false;
}

/**
 * @brief 释放播放器实例及内部资源
 */
static void audio_port_free(void) {
	if (g_port == NULL) return;

	if (g_port->dec_ctx != NULL) {
		g_port->dec->close(g_port->dec_ctx);
	}
	if (g_port->meta != NULL) {
		vPortFree((void*)g_port->meta);
		g_port->meta = NULL;
	}
	if (g_port->sem != NULL) {
		osSemaphoreDelete(g_port->sem);
	}
	vPortFree(g_port->pcm);
	vPortFree(g_port->tx[0]);
	vPortFree(g_port);
	g_port = NULL;
}

/**
 * @brief 关闭播放器并释放全部资源
 *        播放完成、用户中断或任何错误退出后统一调用
 */
static void audio_close(void) {
	HAL_SAI_DMAStop(&hsai_BlockA1);
	audio_port_free();
	tui_clear();
	osEventFlagsClear(System_StatusHandle, APP_NEED_USART);
}

/**
 * @brief 初始化播放器
 * @param path 文件路径
 * @return true成功, false失败(不支持的格式或打开失败)
 */
static bool audio_init(const uint8_t* path) {
	audio_port_t* port = (audio_port_t*)pvPortMalloc(sizeof(audio_port_t));
	if (port == NULL) return false;
	memset(port, 0, sizeof(audio_port_t));
	g_port = port;

	// 根据扩展名查找对应格式的解码器
	const char* ext = strrchr((const char*)path, '.');
	if (ext != NULL) {
#if AUDIO_SUPPORT_WAV
		if (strcmp(ext, ".wav") == 0) g_port->dec = &audio_wav_decoder;
#endif /* AUDIO_SUPPORT_WAV */
#if AUDIO_SUPPORT_MP3
		if (strcmp(ext, ".mp3") == 0) g_port->dec = &audio_mp3_decoder;
#endif /* AUDIO_SUPPORT_MP3 */
#if AUDIO_SUPPORT_FLAC
		if (strcmp(ext, ".flac") == 0) g_port->dec = &audio_flac_decoder;
#endif /* AUDIO_SUPPORT_FLAC */
	}
	if (g_port->dec == NULL) {
		logPrintln("unsupported format");
		audio_port_free();
		return false;
	}

	g_port->dec_ctx = g_port->dec->open(path);
	if (g_port->dec_ctx == NULL) {
		logPrintln("open failed: %s", path);
		audio_port_free();
		return false;
	}

	audio_meta_t* meta = pvPortMalloc(sizeof(audio_meta_t));
	if (meta == NULL) {
		logPrintln("no enough memory for audio meta");
		audio_port_free();
		return false;
	}
	g_port->dec->get_info(g_port->dec_ctx, meta);
	g_port->meta = meta;

	g_port->channels = meta->channels;
	if (g_port->channels == 0 || g_port->channels > AUDIO_PLAY_CH) {
		logPrintln("unsupported channels: %u ch", g_port->channels);
		audio_port_free();
		return false;
	}

	if (!audio_set_freq(meta->sample_rate)) {
		logPrintln("SAI config failed: %uHz", meta->sample_rate);
		audio_port_free();
		return false;
	}

	uint32_t frames = audio_calc_frames(g_port->channels);
	if (frames == 0) {
		logPrintln("no enough memory for audio buffer");
		audio_port_free();
		return false;
	}

	if (!audio_buf_alloc(frames)) {
		logPrintln("audio buffer alloc failed");
		audio_port_free();
		return false;
	}

	return true;
}

/**
 * @brief 填充SAI输出缓冲
 * @param tx 目标缓冲
 * @return true有数据, false文件结束(填充静音)
 */
static bool audio_fill_buf(uint32_t* tx) {
	uint32_t n = g_port->dec->read(g_port->dec_ctx, g_port->pcm, g_port->frames);
	uint32_t samples;
	uint32_t i;

	if (n == 0) {
		memset(tx, 0, g_port->tx_size * sizeof(uint32_t));
		return false;
	}

	// float转16bit并左对齐到32bit slot(应用音量)
	samples = n * g_port->channels;
	for (i = 0; i < samples; i++) {
		int32_t s = (int32_t)(g_port->pcm[i] * volume / 100.0f * 32767.0f);
		if (s > 32767) s = 32767;
		else if (s < -32768) s = -32768;
		tx[i] = (uint32_t)(s << 16);
	}

	// 单声道复制到右声道(交错L,R)
	if (g_port->channels == 1) {
		for (i = n; i > 0; i--) {
			tx[i * 2 - 1] = tx[i - 1];
			tx[i * 2 - 2] = tx[i - 1];
		}
		samples = n * 2;
	}

	// 不足半区补静音
	if (samples < g_port->tx_size) {
		memset(tx + samples, 0, (g_port->tx_size - samples) * sizeof(uint32_t));
	}
	return true;
}

/**
 * @brief 配置PLL3为SAI提供音频主时钟
 * @param series 采样率系列(44100/48000/192000)
 * @return 是否成功
 */
static bool audio_set_pll3(uint32_t series) {
	RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

	PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SAI1;
	PeriphClkInitStruct.PLL3.PLL3M = 25;
	PeriphClkInitStruct.PLL3.PLL3Q = 2;
	PeriphClkInitStruct.PLL3.PLL3R = 2;
	PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_0;
	PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOMEDIUM;

	if (series == 44100) {
		// 44.1k系(11.025k/22.05k/44.1k): PLL3P = 11.2896MHz, Mckdiv=1/2/4
		PeriphClkInitStruct.PLL3.PLL3N = 316;
		PeriphClkInitStruct.PLL3.PLL3FRACN = 891;
		PeriphClkInitStruct.PLL3.PLL3P = 28;
	} else if (series == 192000) {
		// 192k: PLL3P = 49.152MHz, Mckdiv=1
		PeriphClkInitStruct.PLL3.PLL3N = 344;
		PeriphClkInitStruct.PLL3.PLL3FRACN = 524;
		PeriphClkInitStruct.PLL3.PLL3P = 7;
	} else if (series == 48000) {
		// 48k/96k系(8k/16k/32k/48k/96k): PLL3P = 24.576MHz, Mckdiv=12/6/3/2/1
		PeriphClkInitStruct.PLL3.PLL3N = 344;
		PeriphClkInitStruct.PLL3.PLL3FRACN = 524;
		PeriphClkInitStruct.PLL3.PLL3P = 14;
	}

	PeriphClkInitStruct.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLL3;
	return (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) == HAL_OK);
}

/**
 * @brief 设置输出采样率
 * @param sample_rate 采样率(Hz)
 * @return 是否成功
 */
static bool audio_set_freq(uint32_t sample_rate) {
	uint32_t series = 0;

	switch (sample_rate) {
		case SAI_AUDIO_FREQUENCY_8K:
		case SAI_AUDIO_FREQUENCY_16K:
		case SAI_AUDIO_FREQUENCY_32K:
		case SAI_AUDIO_FREQUENCY_48K:
		case SAI_AUDIO_FREQUENCY_96K:
			series = 48000; break;
		case SAI_AUDIO_FREQUENCY_11K:
		case SAI_AUDIO_FREQUENCY_22K:
		case SAI_AUDIO_FREQUENCY_44K:
			series = 44100; break;
		case SAI_AUDIO_FREQUENCY_192K:
			series = 192000; break;
		default: return false;
	}

	if (!audio_set_pll3(series)) {
		return false;
	}

	hsai_BlockA1.Init.AudioFrequency = sample_rate;
	return (HAL_SAI_InitProtocol(&hsai_BlockA1, SAI_I2S_STANDARD, SAI_PROTOCOL_DATASIZE_32BIT, 2) == HAL_OK);
}

/**
 * @brief 播放音频文件
 * @param path 文件路径
 */
static void audio_play(const char* path) {
	// 播放期间禁止其他日志任务抢占串口(界面行刷新依赖稳定的光标位置)
	osEventFlagsSet(System_StatusHandle, APP_NEED_USART);

	if (!audio_init((uint8_t *)path)) {
		osEventFlagsClear(System_StatusHandle, APP_NEED_USART);
		return;
	}

	g_port->sem = osSemaphoreNew(2, 2, NULL);
	if (g_port->sem == NULL) {
		logPrintln("semaphore create failed");
		audio_close();
		return;
	}

	// 预填两个半区并启动DMA循环播放
	g_port->fill_idx = 0;
	audio_fill_buf(g_port->tx[0]);
	osSemaphoreAcquire(g_port->sem, osWaitForever);
	audio_fill_buf(g_port->tx[1]);
	osSemaphoreAcquire(g_port->sem, osWaitForever);

	if (HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t*)g_port->tx[0], g_port->tx_size * 2) != HAL_OK) {
		logPrintln("SAI DMA start failed");
		audio_close();
		return;
	}

	const uint64_t total_frames = g_port->meta->total_frames;
	const uint32_t seek_step = g_port->meta->sample_rate * AUDIO_SEEK_STEP;
	uint32_t play_frames = 0;                          // 已播放帧数
	int64_t seek_frame = -1;                           // 待跳转帧索引(-1无请求)
	uint32_t last_disp = osKernelGetTickCount();       // 上次刷新进度时间(元数据区已由tui_init输出)
	bool eof = false;
	bool abort = false;
	uint32_t silent = 0;
	char key;

	tui_init(path, g_port->meta, volume);

	while (1) {
		// 按键处理: ^C停止, 方向上/下音量, 左/右进度
		while (shell.read(&key, 1) > 0) {
			if (key == 0x03) {
				abort = true;
				break;
			}
			if (key == 0x1B) {
				char seq[2];
				if (shell.read(&seq[0], 1) > 0 && seq[0] == '[' && shell.read(&seq[1], 1) > 0) {
					if (seq[1] == 'A') volume = (volume >= 100) ? 100 : volume + 1;    // 上: 音量+
					else if (seq[1] == 'B') volume = (volume <= 0) ? 0 : volume - 1;   // 下: 音量-
					else if (seq[1] == 'C') seek_frame = (seek_frame >= 0) ?                // 右: 快进(连续按键累加)
						seek_frame + seek_step : play_frames + seek_step;
					else if (seq[1] == 'D') seek_frame = (seek_frame >= 0) ?                // 左: 快退(连续按键累减)
						seek_frame - seek_step : (int64_t)play_frames - seek_step;
					else continue;
					last_disp = 0;   // 按键后立即刷新进度条
				}
			}
		}
		if (abort) break;

		// 执行进度跳转: 先停止SAI发送, seek完成后再重启
		if (seek_frame < -1) seek_frame = 0;
		if (seek_frame >= 0) {
			uint32_t target = (uint32_t)seek_frame;
			if (target >= total_frames) target = (total_frames > 0) ? (uint32_t)total_frames - 1 : 0;

			// 停止SAI发送, 避免seek期间继续播放旧缓冲
			HAL_SAI_DMAStop(&hsai_BlockA1);
			osSemaphoreDelete(g_port->sem);
			g_port->sem = osSemaphoreNew(2, 2, NULL);
			if (g_port->sem == NULL) {
				logPrintln("semaphore recreate failed");
				abort = true;
				break;
			}

			if (g_port->dec->seek(g_port->dec_ctx, target)) {
				play_frames = target;
				eof = false;
				silent = 0;
			}

			// 重新预填两个半区并重启DMA
			g_port->fill_idx = 0;
			audio_fill_buf(g_port->tx[0]);
			osSemaphoreAcquire(g_port->sem, osWaitForever);
			audio_fill_buf(g_port->tx[1]);
			osSemaphoreAcquire(g_port->sem, osWaitForever);
			if (HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t*)g_port->tx[0], g_port->tx_size * 2) != HAL_OK) {
				logPrintln("SAI DMA restart failed");
				abort = true;
				break;
			}
			seek_frame = -1;
		}

		// 等待DMA半区播放完毕(2s超时防卡死)
		if (osSemaphoreAcquire(g_port->sem, 2000) != osOK) {
			break;
		}
		play_frames += g_port->frames;   // 该半区已播放

		if (!audio_fill_buf(g_port->tx[g_port->fill_idx])) {
			// 文件结束: 累计连续静音块数, 待最后有效块播完再停止
			if (eof) {
				if (++silent >= 2) break;
			} else {
				eof = true;
				silent = 0;
			}
		}
		g_port->fill_idx = 1 - g_port->fill_idx;

		// 定时刷新播放器界面
		if (osKernelGetTickCount() - last_disp >= 500) {
			last_disp = osKernelGetTickCount();
			tui_update(play_frames, volume);
		}
	}

	audio_close();
}

/**
 * @brief 显示音频元数据
 * @param path 文件路径
 */
static void audio_info(const char* path) {
	if (!audio_init((uint8_t *)path)) return;

	logPrintln("sample_rate: %uHz", g_port->meta->sample_rate);
	logPrintln("channels: %u", g_port->meta->channels);
	logPrintln("bits_per_sample: %u", g_port->meta->bits_per_sample);
	logPrintln("total_frames: %llu", (unsigned long long)g_port->meta->total_frames);
	logPrintln("duration: %um%us", g_port->meta->duration_ms / 60000, (g_port->meta->duration_ms / 1000) % 60);

	audio_port_free();
}

/**
 * @brief 显示audio工具帮助信息
 */
static void audio_help(void) {
	logPrintln(
		"Usage: audio [OPTION] FILE\r\n"
		"  -h, --help       show this help\r\n"
		"  -i               show audio file metadata\r\n"
		"  FILE             play audio file"
	);
}

/**
 * @brief audio工具主函数，解析指令并分发
 */
static void audio(int argc, char *argv[]) {
	uint8_t i;
	uint8_t k;

	if (argc < 2) {
		audio_help();
		return;
	}

	// 通用参数解析：区分选项和位置参数(文件)
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1] != '\0') {
			// 选项参数
			if (argv[i][1] == '-') {
				// 长选项
				if (strcmp(argv[i], "--help") == 0) {
					audio_help(); return;
				}
				logPrintln("invalid option -- '%s'", argv[i]);
				audio_help(); return;
			}
			// 短选项
			for (k = 1; argv[i][k] != '\0'; k++) {
				switch (argv[i][k]) {
					case 'h': audio_help(); return;
					case 'i':
						// -i后跟音频文件
						if (i + 2 != argc) {
							logPrintln("Usage: audio -i <path>");
							return;
						}
						audio_info(argv[i+1]);
						return;
					default:
						logPrintln("invalid option -- '%c'", argv[i][k]);
						audio_help(); return;
				}
			}
		} else {
			// 位置参数，视为播放文件
			if (argc != 2) {
				logPrintln("Usage: audio <path>");
				return;
			}
			audio_play(argv[i]);
			return;
		}
	}
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
audio, audio, Audio Player);


/**
 * @brief SAI DMA半区传输完成回调
 * @param hsai SAI句柄
 */
void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef* hsai) {
	if (hsai->Instance == SAI1_Block_A && g_port != NULL && g_port->sem != NULL) {
		osSemaphoreRelease(g_port->sem);
	}
}

/**
 * @brief SAI DMA整区传输完成回调
 * @param hsai SAI句柄
 */
void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef* hsai) {
	if (hsai->Instance == SAI1_Block_A && g_port != NULL && g_port->sem != NULL) {
		osSemaphoreRelease(g_port->sem);
	}
}
