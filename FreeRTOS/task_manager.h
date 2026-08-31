#ifndef __TASK_TABLE_H__
#define __TASK_TABLE_H__

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"

//判断是否在中断里面
#define is_in_irq() (__get_IPSR() != 0U)
#define os_time_dly(x) vTaskDelay(x)

//计数信号量的最大计算
#define MAX_SEM_COUNT 100

/* taskq 消息类型, 取值与杰理 SDK(os_type.h) 保持一致, 方便 703 的代码直接移植。
 * 队列里每条消息的头部 = 类型(高12位) | 参数个数(低20位),
 * os_taskq_pend 把这个头原样放在 argv[0] 返回, 参数从 argv[1] 开始 */
#define Q_MSG               0x100000
#define Q_EVENT             0x200000
#define Q_CALLBACK          0x300000
#define Q_USER              0x400000

#define Q_TYPE_MASK         0xFFF00000
#define Q_ARGC_MASK         0x000FFFFF

/* 从 os_taskq_pend 返回的消息头 argv[0] 中取出类型和参数个数 */
#define Q_TYPE(header)      ((int)(header) & Q_TYPE_MASK)
#define Q_ARGC(header)      ((int)(header) & Q_ARGC_MASK)

/* Q_CALLBACK 回调支持的最大参数个数(每个参数按 int 宽度传递)。
 * 杰理没有这个宏, 取 8 是对齐 os_api.h 里 "最大参数个数限制为8个int" 的说法,
 * 也覆盖了 703 rcsp_event.c 里 APP_RCSP_MSG_VAL_MAX(8) 那条最长的调用链 */
#define Q_CALLBACK_ARGC_MAX 8

struct task_info {
    const char *name;
    uint8_t priority;
    uint16_t stack_size;
    uint16_t qsize;
};

struct task_handle {
    TaskHandle_t *pxCreatedTask;
    xQueueHandle *xQueue;
};

//临界区保护接口
void os_task_enter_critical(UBaseType_t *pxSavedInterruptStatus);
void os_task_exit_critical(UBaseType_t *pxSavedInterruptStatus);

/* 创建任务
 * @param task 任务函数
 * @param p 任务参数
 * @param name 任务名称
 * @return pdPASS:成功，pdFAIL:失败
 */
int task_create(void (*task)(void *p), void *p, const char *name);

/* 启动任务调度器
 * @param void
 * @return void
 * note:
 */
void os_start(void);

/* 删除任务
 * @param name 任务名称
 * @return pdPASS 成功，pdPASS 失败
 */
int task_kill(const char *name);

/* 往任务消息队尾发送消息
 * @param name 任务名称
 * @param argc 参数个数
 * @param ... 参数列表
 * @return pdPASS 成功
 * @return pdFAIL 失败
 * @note 可以在任务或者中断里面使用
 */
BaseType_t os_taskq_post_msg(const char *name, int argc, ...);

/* 往任务消息队头发送消息
 * @param name 任务名称
 * @param argc 参数个数
 * @param ... 参数列表
 * @return pdPASS 成功
 * @return pdFAIL 失败
 * @note 可以在任务或者中断里面使用
 */
BaseType_t os_taskq_post_msg_front(const char *name, int argc, ...);

/* 往任务消息队尾发送指定类型的消息
 * @param name 任务名称
 * @param type 消息类型 Q_MSG / Q_EVENT / Q_CALLBACK / Q_USER
 * @param argc 参数个数
 * @param argv 参数数组
 * @return pdPASS 成功
 * @return pdFAIL / errQUEUE_FULL 失败
 * @note 可以在任务或者中断里面使用
 * @note Q_CALLBACK 的参数约定与杰理 SDK 一致: argv[0]=函数指针,
 *       argv[1]=回调参数个数, argv[2...]=回调参数, 即 argc = 回调参数个数 + 2
 * @note 返回值语义和杰理相反! 杰理是返回 0 表示成功, 这里是返回 pdPASS(1)
 *       表示成功, 移植 703 代码时 if (ret) 这类判断要跟着改
 */
BaseType_t os_taskq_post_type(const char *name, int type, int argc, int *argv);

/* 往任务发送一个回调消息(Q_CALLBACK), 由目标任务在自己的上下文里执行 func
 * @param name 任务名称
 * @param func 回调函数, 参数只能是 int 宽度(指针/整型), 最多 Q_CALLBACK_ARGC_MAX 个
 * @param nargs 回调参数个数
 * @param ... 回调参数列表
 * @return pdPASS 成功, 其他为失败
 * @note 可以在任务或者中断里面使用
 * @note 用于把中断/其他任务里的处理搬到目标任务上下文执行。若参数是 malloc
 *       出来的内存, 发送失败时调用方必须自己 free, 否则内存泄漏
 */
BaseType_t os_taskq_post_callback(const char *name, void *func, int nargs, ...);

/* @任务获取消息
 * @param argv 获取消息buf
 * @param argc 获取消息buf长度
 * @param timeout_ms 获取消息超时时间
 * @return 成功返回pdPASS，失败返回pdFAIL
 * @note 不能再中断里面使用   
 */
BaseType_t os_taskq_pend(int *argv, int argc, TickType_t xTicksToWait);

//TODO 任务非阻塞式获取消息
int os_taskq_accept(int argc, int *argv);

/* 清空当前任务的消息队列
 * @return pdPASS:成功，pdFAIL:失败
 * @note: 不能在中断里面使用
 */
BaseType_t task_queue_clear(void);

/* 创建信号量
 * @param sem: 信号量句柄
 * @param count: 初始计数
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 不能在中断里面使用
 */
int os_sem_create(SemaphoreHandle_t *sem, int count);

/* 删除信号量，释放内存
 * @param sem: 信号量句柄
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 不能在中断里面使用
 */
int os_sem_delete(SemaphoreHandle_t *sem);

/* 释放信号量
 * @param sem: 信号量句柄
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 可以在任务或者中断里面使用
 */
int os_sem_post(SemaphoreHandle_t *sem);
/* 获取信号量
 * @param sem: 信号量句柄
 * @param timeout_ms: 超时时间，单位ms
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 不能在中断里面使用
 */
int os_sem_pend(SemaphoreHandle_t *sem, uint32_t timeout_ms);

/* 创建互斥量
 * @param mutex: 互斥量句柄
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 不能在中断里面使用
 */
int os_mutex_create(SemaphoreHandle_t *mutex);

/* 删除互斥量，释放内存
 * @param mutex: 互斥量句柄
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 不能在中断里面使用
 */
int os_mutex_delete(SemaphoreHandle_t *mutex);

/* 释放互斥量
 * @param mutex: 互斥量句柄
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 可以在任务或者中断里面使用
 */
int os_mutex_post(SemaphoreHandle_t *mutex);

/* 获取互斥量
 * @param mutex: 互斥量句柄
 * @param timeout_ms: 超时时间，单位ms
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 不能在中断里面使用
 */
int os_mutex_pend(SemaphoreHandle_t *mutex, uint32_t timeout_ms);

/* 创建软件周期定时器
 * @param p: 保留参数
 * @param func: 定时器回调函数
 * @param msec: 定时器时间，单位ms
 * @return: 定时器id，0表示失败
 * @note: 不能在中断里面使用
 */
uint32_t sys_timer_add(void *p, void (*func)(void *priv), uint32_t msec);

/* 删除软件定时器
 * @param timer_id: 定时器id
 * @return: 无
 * @note: 不能在中断里面使用
 */
void sys_timer_del(uint32_t timer_id);

 /* 添加单次软件定时器
 * @param p: 保留参数
 * @param func: 定时器回调函数
 * @param msec: 定时器时间，单位毫秒
 * @return: 定时器id，0表示失败
 * @note: 不能在中断里面使用
 */
uint32_t sys_timeout_add(void *p, void (*func)(void *priv), uint32_t msec);

/* 删除周期性软件定时器
 * @param timer_id: 定时器id
 * @return:
 * @note: 不能在中断里面使用
 */
void sys_timeout_del(uint32_t timer_id);

/* 修改软件定时器时间
 * @param timer_id: 定时器id
 * @param msec: 定时器时间，单位ms
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 
 */
int sys_timer_modify(uint32_t timer_id, uint32_t msec);

/* 重启软件定时器。重新开始计时
 * @param timer_id: 定时器id
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 
 */
int sys_timer_reset(uint32_t timer_id);

void sys_mem_dump(void);
void task_info_dump(void);

#endif // !__TASK_TABLE_H__

