/**
 * @file lvgl_user.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief LVGL用户接口源文件
 */

#include "lvgl_user.h"
#include "lvgl.h"
#include "H7_MediaPlayer_UI.h"


/**
 * @brief LVGL用户界面初始化
 */
void lvgl_user_init(void) {
	lv_obj_t *home;

	/* 初始化LVGL Editor工程（主题、字体、翻译资源） */
	H7_MediaPlayer_UI_init(NULL);

	/* 创建home界面并加载，其动画时间轴在LV_EVENT_SCREEN_LOADED时自动播放 */
	home = home_create();
	if(home != NULL) lv_screen_load(home);
}
