#ifndef _APPS_H__
#define _APPS_H__
#include "typedef.h"
#include "task_manager.h"



enum {
    APP_MSG_SYS_EVENT = 0X1000,
    APP_MSG_KEY_EVENT,
};

enum {
    KEY_MUSIC_PP,
    KEY_MUSIC_PREV,
    KEY_MUSIC_NEXT,
    KEY_MUSIC_FF,
    KEY_MUSIC_FR,
    KEY_PAGE_ENTER,
    KEY_PAGE_BACK,
    KEY_PAGE_PREV,
    KEY_PAGE_NEXT,

    KEY_NULL = 0xFFFF,

};


#define app_msg_post(argc, ...)  os_taskq_post_msg("app_core", (argc)+1, APP_MSG_KEY_EVENT, ##__VA_ARGS__)

/* 把 func 丢到 app_core 任务上下文里执行, 常用于中断/其他任务里不方便直接做的事
 * @param func 回调函数, 参数只能是 int 宽度(指针/整型)
 * @param nargs 回调参数个数, 最多 Q_CALLBACK_ARGC_MAX 个
 */
#define app_callback_post(func, nargs, ...)      os_taskq_post_callback("app_core", (void *)(func), (nargs), ##__VA_ARGS__)



void app_core_init(void);
//BaseType_t app_msg_post(int argc, ...);



#endif // _APPS_H__
