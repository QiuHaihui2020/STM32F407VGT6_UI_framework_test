/**
 * @file    jl_ui_api.h
 * @brief   UI 框架对外 API(应用层调这些来显示界面)
 */
#ifndef __JL_UI_API_H__
#define __JL_UI_API_H__

#include "ui_port_config.h"
#include "jl_typedef.h"
#include "jl_lcd_drive.h"
#include "ui/ui.h"
#include "ui_style.h"

#define GRT_CUR_MENU    (0)
#define GET_MAIN_MENU   (1)

enum ui_devices_type {
    LED_7,
    LCD_SEG3X9,
    TFT_LCD,    /**< 彩屏 */
    DOT_LCD,    /**< 点阵屏, 本移植用这个 */
};

/** 板级配置。lcd_ui_init 的入参 */
struct ui_devices_cfg {
    enum ui_devices_type type;
    void *private_data;     /**< 指向 struct lcd_platform_data */
};

struct touch_event {
    int event;
    int x;
    int y;
    int has_energy;
};

/** 板级配置实例, 定义在 config/ui_port_registry.c */
extern const struct ui_devices_cfg ui_cfg_data;

/* ---- 主要 API(实现在 lcd_drive/middle/lcd_ui_api.c) ------------------------- */
int  lcd_ui_init(void *arg);
int  ui_show_main(int id);
int  ui_hide_main(int id);

int  ui_hide_curr_main(void);
int  ui_server_msg_post(const char *msg, ...);
int  ui_touch_msg_post(struct touch_event *event);
int  ui_key_msg_post(int msg);
int  ui_simple_key_msg_post(int a, int b);
void key_ui_takeover(u8 on);
int  key_is_ui_takeover(void);
void ui_backlight_open(u8 recover_cur_page);
void ui_backlight_close(void);

void ui_touch_timer_delete(void);
void ui_touch_timer_start(void);
void ui_auto_shut_down_modify(void);
void ui_auto_shut_down_enable(void);
u8   ui_auto_shut_down_disable(void);

typedef enum {
    SLIDE_MODE_OFF,
    SLIDE_MODE_NOT_RIGHT_ALL,
    SLIDE_MODE_NOT_RIGHT_FIRST,
    SLIDE_MODE_NOT_LOOP,
} CARD_SLIDE_MODE;


extern const struct ui_devices_cfg ui_cfg_data;

/* ---- 应用层统一入口宏 ----------------------------------------------- */
#if (TCFG_UI_ENABLE)
#define UI_INIT(a)              lcd_ui_init(a)
#define UI_SHOW_WINDOW(a)       ui_show_main(a)
#define UI_HIDE_WINDOW(a)       ui_hide_main(a)
#define UI_HIDE_CURR_WINDOW()   ui_hide_curr_main()
#define UI_GET_WINDOW_ID()      ui_get_current_window_id()
#define UI_MSG_POST             ui_server_msg_post
#define UI_KEY_MSG_POST(a)      ui_key_msg_post(a)
#define UI_SHOW_MENU(...)
#define UI_GET_CURR_MENU()
#define UI_REFLASH_WINDOW(a)
#else
#define UI_INIT(...)
#define UI_SHOW_WINDOW(...)
#define UI_HIDE_WINDOW(...)
#define UI_HIDE_CURR_WINDOW()
#define UI_GET_WINDOW_ID()
#define UI_MSG_POST(...)
#define UI_KEY_MSG_POST(...)
#define UI_SHOW_MENU(...)
#define UI_GET_CURR_MENU()
#define UI_REFLASH_WINDOW(a)
#endif

#endif /* __JL_UI_API_H__ */
