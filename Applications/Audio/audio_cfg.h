/**
 * @file audio_cfg.h
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief 音频播放器配置文件
 */


/* -------------- 播放器功能配置 -------------- */
#define AUDIO_SUPPORT_WAV 1     // 启用wav格式音频解码支持
#define AUDIO_SUPPORT_MP3 1     // 启用mp3格式音频解码支持
#define AUDIO_SUPPORT_FLAC 1    // 启用flac格式音频解码支持
#define AUDIO_SUPPORT_LRC 1     // 启用歌词显示
#define AUDIO_FINDLRC_ONFILE 1  // 是否支持外部歌词文件查找


/* -------------- 播放器解码配置 -------------- */
#define AUDIO_SKEEP_FAILFRAME 1 // 是否在解码失败时 跳过当前帧重新同步(仅flac)
#define AUDIO_RESERVED_MEM (2 * 1024)        // 为系统运行保留的内存
#define AUDIO_PLAY_CH 2                      // SAI声道数


/* -------------- 播放器界面配置 -------------- */
#define AUDIO_TUI_LYRIC_ROWS 5               // 播放器界面歌词区行数(歌词同步未实现, 预留)
#define AUDIO_TUI_WIDTH      56              // 界面行显示宽度(整行底色填充)
#define AUDIO_PROG_BAR_LEN 30                // 播放器界面进度条格数
#define AUDIO_VOL_BAR_LEN 12                 // 播放器界面音量条格数


#define AUDIO_SEEK_STEP 5                    // 左右键跳进步长(秒)
