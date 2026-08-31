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

