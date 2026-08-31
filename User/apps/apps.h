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



void app_core_init(void);
//BaseType_t app_msg_post(int argc, ...);



#endif // _APPS_H__
