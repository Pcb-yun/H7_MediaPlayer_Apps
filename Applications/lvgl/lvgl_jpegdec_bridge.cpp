/**
 * @file lvgl_jpegdec_bridge.cpp
 * @brief JPEGDEC 与 LVGL 之间的桥接层
 *
 * 将 BitBank JPEGDEC（C++ 类）包装为一组 C 接口供 lvgl_jpegdec_port.c 调用。
 * JPEGDEC 实例内含十余 KB 的解码工作区，不可置于栈上；它与解码输出条带
 * 缓冲一并放入 DTCM，以降低对主 RAM 带宽的争抢。
 *
 * 解码策略：
 * JPEGDEC 的 decode() 为阻塞式整图解码，每次进入都会把比特流重置到文件头部，
 * 无法跨调用续解。本桥接层因此采用“整宽条带”输出：每次按请求的行范围重新
 * 解码，并在 DRAW 回调中越过条带底部时返回 0 提前终止解码，从而把单次开销
 * 限制在“解码到该条带底部”所需的行数。
 */

#include "lvgl_jpegdec_port.h"

#include "JPEGDEC.h"
#include "lvgl.h"

#include <string.h>

#define JPEGDEC_BRIDGE_BAND_WIDTH	240		// 解码输出条带的最大宽度（像素）
#define JPEGDEC_BRIDGE_BAND_HEIGHT	64		// 解码输出条带的最大高度（行），越大则整幅图需要的重解码次数越少
#define JPEGDEC_BRIDGE_ROW_ALIGN	16		// 条带起始行对齐粒度，需覆盖所有子采样下的 MCU 行高

/** JPEGDEC 实例，含解码工作区，不可置于栈上 */
static __attribute__((section(".DTCM"))) JPEGDEC s_jpeg;
/** 解码输出条带缓冲，RGB565 格式 */
static __attribute__((section(".DTCM"))) uint16_t s_band_pixels[JPEGDEC_BRIDGE_BAND_WIDTH * JPEGDEC_BRIDGE_BAND_HEIGHT];

static void * s_file_handle = NULL;		// 底层文件对象（lv_fs_file_t 指针）
static int32_t s_data_size = 0;			// JPEG 数据流总长度（字节）
static bool s_opened = false;			// 数据流是否已打开

static int32_t s_scale = 1;			// 快速缩放倍数：1/2/4/8
static int32_t s_options = 0;			// 传给 JPEGDEC::decode 的缩放选项
static int32_t s_out_width = 0;			// 缩放后输出宽度（像素）
static int32_t s_out_height = 0;		// 缩放后输出高度（行）

static int32_t s_band_y = 0;			// 当前条带在输出图中的起始行
static int32_t s_band_h = 0;			// 当前条带高度（行）
static int32_t s_band_w = 0;			// 当前条带宽度（像素）

/**
 * @brief JPEGDEC 读回调，从底层文件流读取数据
 * @param p_file JPEGDEC 文件对象
 * @param p_buf 读取缓冲区
 * @param i_len 期望读取的字节数
 * @return int32_t 实际读取的字节数
 */
static int32_t bridge_read_cb(JPEGFILE * p_file, uint8_t * p_buf, int32_t i_len)
{
	uint32_t read_bytes = 0;

	if(lv_fs_read((lv_fs_file_t *)p_file->fHandle, p_buf, (uint32_t)i_len, &read_bytes) != LV_FS_RES_OK)
		return 0;

	p_file->iPos += (int32_t)read_bytes;
	return (int32_t)read_bytes;
}

/**
 * @brief JPEGDEC 定位回调，把底层文件流定位到指定位置
 * @param p_file JPEGDEC 文件对象
 * @param i_position 目标位置（相对文件头）
 * @return int32_t 定位后的位置，失败返回-1
 */
static int32_t bridge_seek_cb(JPEGFILE * p_file, int32_t i_position)
{
	if(lv_fs_seek((lv_fs_file_t *)p_file->fHandle, (uint32_t)i_position, LV_FS_SEEK_SET) != LV_FS_RES_OK)
		return -1;

	p_file->iPos = i_position;
	return i_position;
}

/**
 * @brief JPEGDEC 关闭回调，关闭底层文件流
 * @param p_handle 底层文件对象
 */
static void bridge_close_cb(void * p_handle)
{
	lv_fs_close((lv_fs_file_t *)p_handle);
}

/**
 * @brief JPEGDEC 绘图回调，把当前 MCU 行块中落在目标条带内的部分拷入条带缓冲
 * @param p_draw JPEGDEC 绘图描述块
 * @return int 返回0表示请求JPEGDEC提前终止解码，非0表示继续
 * @note 返回0是JPEGDEC唯一的提前退出手段，此时decode()仍返回成功
 */
static int bridge_draw_cb(JPEGDRAW * p_draw)
{
	int32_t row = (int32_t)p_draw->y - s_band_y;
	int32_t col = (int32_t)p_draw->x;
	int32_t width = (int32_t)p_draw->iWidthUsed;
	int32_t height = (int32_t)p_draw->iHeight;
	int32_t first_row = 0;

	if(row >= s_band_h)
		return 0;		// 已越过目标条带底部，提前终止
	if(row + height <= 0 || col >= s_band_w)
		return 1;		// 分块不与目标条带相交
	if(row < 0) {
		first_row = -row;
		row = 0;
	}
	if(height - first_row > s_band_h - row)
		height = first_row + s_band_h - row;

	if(width > s_band_w - col)
		width = s_band_w - col;
	if(width > 0) {
		for(int32_t src_row = first_row; src_row < height; src_row++) {
			memcpy(&s_band_pixels[(row + src_row - first_row) * s_band_w + col],
			       &p_draw->pPixels[src_row * p_draw->iWidth], (size_t)width * sizeof(uint16_t));
		}
	}

	return 1;
}

/**
 * @brief 依据当前缩放倍数刷新缩放后的输出尺寸
 */
static void bridge_update_out_size(void)
{
	if(!s_opened) {
		s_out_width = 0;
		s_out_height = 0;
		return;
	}

	s_out_width = (s_jpeg.getWidth() + s_scale - 1) / s_scale;
	s_out_height = (s_jpeg.getHeight() + s_scale - 1) / s_scale;
}

bool lvgl_jpegdec_bridge_open(void * f_handle, int32_t i_data_size)
{
	if(s_opened || f_handle == NULL || i_data_size <= 0)
		return false;

	s_file_handle = f_handle;
	s_data_size = i_data_size;
	s_scale = 1;
	s_options = 0;

	// JPEGDEC 每次 open 都会把内部文件位置清零，这里同步把底层流定位到文件头
	if(lv_fs_seek((lv_fs_file_t *)f_handle, 0, LV_FS_SEEK_SET) != LV_FS_RES_OK)
		return false;

	if(!s_jpeg.open(f_handle, i_data_size, bridge_close_cb, bridge_read_cb, bridge_seek_cb, bridge_draw_cb))
		return false;
	// 即使采用最大缩放倍数，超过条带宽度的图像也无法完整输出。
	if(s_jpeg.getWidth() <= 0 || s_jpeg.getHeight() <= 0 ||
	   s_jpeg.getWidth() > JPEGDEC_BRIDGE_BAND_WIDTH * 8)
		return false;

	s_opened = true;
	if(s_jpeg.getJPEGType() == JPEG_MODE_PROGRESSIVE) {
		s_scale = 8;
		s_options = JPEG_SCALE_EIGHTH;
	}
	bridge_update_out_size();
	return true;
}

void lvgl_jpegdec_bridge_close(void)
{
	if(!s_opened)
		return;

	s_jpeg.close();
	s_opened = false;
	s_file_handle = NULL;
	s_data_size = 0;
	s_out_width = 0;
	s_out_height = 0;
}

void lvgl_jpegdec_bridge_set_scale(int32_t i_scale)
{
	// JPEGDEC 对渐进式 JPEG 始终强制输出 1/8 尺寸。
	if(s_opened && s_jpeg.getJPEGType() == JPEG_MODE_PROGRESSIVE)
		i_scale = 8;

	switch(i_scale) {
		case 2:
			s_options = JPEG_SCALE_HALF;
			break;
		case 4:
			s_options = JPEG_SCALE_QUARTER;
			break;
		case 8:
			s_options = JPEG_SCALE_EIGHTH;
			break;
		default:
			i_scale = 1;
			s_options = 0;
			break;
	}

	s_scale = i_scale;
	bridge_update_out_size();
}

int32_t lvgl_jpegdec_bridge_get_width(void)
{
	return s_opened ? (int32_t)s_jpeg.getWidth() : 0;
}

int32_t lvgl_jpegdec_bridge_get_height(void)
{
	return s_opened ? (int32_t)s_jpeg.getHeight() : 0;
}

int32_t lvgl_jpegdec_bridge_get_out_width(void)
{
	return s_out_width;
}

int32_t lvgl_jpegdec_bridge_get_out_height(void)
{
	return s_out_height;
}

int32_t lvgl_jpegdec_bridge_get_band_height(void)
{
	return JPEGDEC_BRIDGE_BAND_HEIGHT;
}
