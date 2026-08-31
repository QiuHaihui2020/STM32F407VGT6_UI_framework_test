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

/* taskq 消息类型, 取值与杰理 SDK(os_type.h) 保持一致, 方便 703 的代码直接移植 */
#define Q_MSG               0x100000
#define Q_EVENT             0x200000
#define Q_CALLBACK          0x300000
#define Q_USER              0x400000

#define Q_TYPE_MASK         0xFFF00000

/* 从 os_taskq_pend 返回的 argv[0] 中取出消息类型。
 *
 * argv[0] 就是发送时传入的 type 本身(完整 32 位), 参数从 argv[1] 开始
 * —— 与杰理 os_taskq_pend 的布局一致。
 *
 * 所以:
 *   - 用 Q_MSG / Q_EVENT / Q_CALLBACK / Q_USER 这类高位常量做类型时,
 *     Q_TYPE(argv[0]) 照常可用;
 *   - 用小整数(如自定义的 0、1、2...)做类型时, 直接比 argv[0] 即可,
 *     不要套 Q_TYPE() —— 那会把低位掩掉。
 *
 * @note 早先的格式是把类型和参数个数塞进同一个字(类型只占高 12 位),
 *       小整数类型会被整个抹掉。现在头部是两个字, 类型独占一个,
 *       格式定义见 task_manager.c 的 taskq_send_msg()。
 *       原来的 Q_ARGC(argv[0]) 已随之失效并移除: 参数个数只在队列内部使用,
 *       调用方本来就知道自己发了几个参数。 */
#define Q_TYPE(header)      ((int)(header) & Q_TYPE_MASK)

/* ======================================================================
 * 错误码 —— 照抄杰理 SDK interface/system/os/os_error.h 的【枚举顺序】
 *
 * 顺序不能动: 杰理代码里会直接比 OS_TASKQ / OS_TIMEOUT / OS_Q_FULL 这些名字,
 * 取值必须和 703 一致(OS_TASKQ = 13, OS_TIMEOUT = 11, ...)。
 *
 * 本工程 task_manager 的所有 API 都返回这套错误码, 与杰理一致:
 *   【0(OS_NO_ERR) = 成功, 非 0 = 失败】
 *
 * ⚠ 这与 FreeRTOS 原生的 pdPASS(1)=成功 【相反】。task_manager.c 内部会做
 *   翻译, 调用方一律按 `if (ret) { 失败 }` 或 `if (ret != OS_NO_ERR)` 判断,
 *   不要再拿 pdPASS 比。
 * ====================================================================== */
#define OS_ERR_NONE   0

enum {
    OS_NO_ERR = 0,
    OS_TRUE,
    OS_ERR_EVENT_TYPE,
    OS_ERR_PEND_ISR,
    OS_ERR_POST_NULL_PTR,
    OS_ERR_PEVENT_NULL,
    OS_ERR_POST_ISR,
    OS_ERR_QUERY_ISR,
    OS_ERR_INVALID_OPT,
    OS_ERR_TASK_WAITING,
    OS_ERR_PDATA_NULL,
    OS_TIMEOUT,
    OS_TIMER,
    OS_TASKQ,                   /* os_taskq_pend 取到消息时返回这个, 不是 0 */
    OS_TASK_NOT_EXIST,
    OS_ERR_EVENT_NAME_TOO_LONG,
    OS_ERR_FLAG_NAME_TOO_LONG,
    OS_ERR_TASK_NAME_TOO_LONG,
    OS_ERR_PNAME_NULL,
    OS_ERR_TASK_CREATE_ISR,
    OS_MBOX_FULL,
    OS_Q_FULL,
    OS_Q_EMPTY,
    OS_Q_ERR,
    OS_ERR_NO_QBUF,
    OS_PRIO_EXIST,
    OS_PRIO_ERR,
    OS_PRIO_INVALID,
    OS_SEM_OVF,
    OS_TASK_DEL_ERR,
    OS_TASK_DEL_IDLE,
    OS_TASK_DEL_ISR,
    OS_NO_MORE_TCB,
    OS_TIME_NOT_DLY,
    OS_TIME_INVALID_MINUTES,
    OS_TIME_INVALID_SECONDS,
    OS_TIME_INVALID_MILLI,
    OS_TIME_ZERO_DLY,
    OS_TASK_SUSPEND_PRIO,
    OS_TASK_SUSPEND_IDLE,
    OS_TASK_RESUME_PRIO,
    OS_TASK_NOT_SUSPENDED,
    OS_MEM_INVALID_PART,
    OS_MEM_INVALID_BLKS,
    OS_MEM_INVALID_SIZE,
    OS_MEM_NO_FREE_BLKS,
    OS_MEM_FULL,
    OS_MEM_INVALID_PBLK,
    OS_MEM_INVALID_PMEM,
    OS_MEM_INVALID_PDATA,
    OS_MEM_INVALID_ADDR,
    OS_MEM_NAME_TOO_LONG,
    OS_ERR_MEM_NO_MEM,
    OS_ERR_NOT_MUTEX_OWNER,
    OS_TASK_OPT_ERR,
    OS_ERR_DEL_ISR,
    OS_ERR_CREATE_ISR,
    OS_FLAG_INVALID_PGRP,
    OS_FLAG_ERR_WAIT_TYPE,
    OS_FLAG_ERR_NOT_RDY,
    OS_FLAG_INVALID_OPT,
    OS_FLAG_GRP_DEPLETED,
    OS_ERR_PIP_LOWER,
    OS_ERR_MSG_POOL_EMPTY,
    OS_ERR_MSG_POOL_NULL_PTR,
    OS_ERR_MSG_POOL_FULL,

};

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
 * @return OS_NO_ERR(0):成功, 非 0:错误码
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
 * @return OS_NO_ERR(0) 成功, 非 0 错误码
 */
int task_kill(const char *name);

/* 往任务消息队尾发送消息
 * @param name 任务名称
 * @param argc 参数个数
 * @param ... 参数列表
 * @return OS_NO_ERR(0) 成功, 非 0 错误码(OS_Q_FULL / OS_TASK_NOT_EXIST ...)
 * @note 可以在任务或者中断里面使用
 */
int os_taskq_post_msg(const char *name, int argc, ...);

/* 往任务消息队头发送消息
 * @param name 任务名称
 * @param argc 参数个数
 * @param ... 参数列表
 * @return OS_NO_ERR(0) 成功, 非 0 错误码(OS_Q_FULL / OS_TASK_NOT_EXIST ...)
 * @note 可以在任务或者中断里面使用
 */
int os_taskq_post_msg_front(const char *name, int argc, ...);

/* 往任务消息队尾发送指定类型的消息
 * @param name 任务名称
 * @param type 消息类型 Q_MSG / Q_EVENT / Q_CALLBACK / Q_USER
 * @param argc 参数个数
 * @param argv 参数数组
 * @return OS_NO_ERR(0) 成功, 非 0 错误码(OS_Q_FULL / OS_TASK_NOT_EXIST ...)
 * @note 可以在任务或者中断里面使用
 * @note type 可以是 Q_MSG 这类高位常量, 也可以是任意小整数 ——
 *       它独占消息头的一个字, 不会被截断。接收方从 argv[0] 原样拿到。
 * @note Q_CALLBACK 的参数约定与杰理 SDK 一致: argv[0]=函数指针,
 *       argv[1]=回调参数个数, argv[2...]=回调参数, 即 argc = 回调参数个数 + 2
 */
int os_taskq_post_type(const char *name, int type, int argc, int *argv);

/* 往任务发送一个回调消息(Q_CALLBACK), 由目标任务在自己的上下文里执行 func
 * @param name 任务名称
 * @param func 回调函数, 参数只能是 int 宽度(指针/整型), 最多 Q_CALLBACK_ARGC_MAX 个
 * @param nargs 回调参数个数
 * @param ... 回调参数列表
 * @return OS_NO_ERR(0) 成功, 非 0 错误码
 * @note 可以在任务或者中断里面使用
 * @note 用于把中断/其他任务里的处理搬到目标任务上下文执行。若参数是 malloc
 *       出来的内存, 发送失败时调用方必须自己 free, 否则内存泄漏
 */
int os_taskq_post_callback(const char *name, void *func, int nargs, ...);

/* @任务获取消息
 * @param argv 获取消息buf。返回时 argv[0] = 发送方传入的 type,
 *             参数从 argv[1] 开始(与杰理 os_taskq_pend 布局一致)
 * @param argc 获取消息buf长度, 单位【字节】(所以一般传 sizeof(buf))
 * @param timeout_ms 获取消息超时时间, portMAX_DELAY 为一直等待
 * @return OS_TASKQ(13) 取到消息; 其它值表示没取到(OS_TIMEOUT / OS_Q_ERR ...)
 * @note 不能在中断里面使用
 * @note Q_CALLBACK 类型的消息在本函数内部直接执行掉, 不会返回给调用者,
 *       执行完继续等待下一条消息
 */
int os_taskq_pend(int *argv, int argc, TickType_t xTicksToWait);

//TODO 任务非阻塞式获取消息
int os_taskq_accept(int argc, int *argv);

/* 清空当前任务的消息队列
 * @return OS_NO_ERR(0):成功, 非 0:错误码
 * @note: 不能在中断里面使用
 */
int task_queue_clear(void);

/* 创建信号量
 * @param sem: 信号量句柄
 * @param count: 初始计数
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 不能在中断里面使用
 */
int os_sem_create(SemaphoreHandle_t *sem, int count);

/* 删除信号量，释放内存
 * @param sem: 信号量句柄
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 不能在中断里面使用
 */
int os_sem_delete(SemaphoreHandle_t *sem);

/* 释放信号量
 * @param sem: 信号量句柄
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 可以在任务或者中断里面使用
 */
int os_sem_post(SemaphoreHandle_t *sem);
/* 获取信号量
 * @param sem: 信号量句柄
 * @param timeout_ms: 超时时间, 单位 ms。
 *                     【0 表示一直等待】—— 这是杰理的约定, 与 FreeRTOS
 *                     原生的"0 tick = 不等待"相反, 移植过来的代码大量依赖它
 *                     (如 lcd_ui_api.c 的 lcd_ui_init 等 ui_task 启动完成)。
 * @return: OS_NO_ERR(0)-成功, OS_TIMEOUT-超时
 * @note: 不能在中断里面使用
 */
int os_sem_pend(SemaphoreHandle_t *sem, uint32_t timeout_ms);

/* 创建互斥量
 * @param mutex: 互斥量句柄
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 不能在中断里面使用
 */
int os_mutex_create(SemaphoreHandle_t *mutex);

/* 删除互斥量，释放内存
 * @param mutex: 互斥量句柄
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 不能在中断里面使用
 */
int os_mutex_delete(SemaphoreHandle_t *mutex);

/* 释放互斥量
 * @param mutex: 互斥量句柄
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 可以在任务或者中断里面使用
 */
int os_mutex_post(SemaphoreHandle_t *mutex);

/* 获取互斥量
 * @param mutex: 互斥量句柄
 * @param timeout_ms: 超时时间, 单位 ms。【0 表示一直等待】(同 os_sem_pend)
 * @return: OS_NO_ERR(0)-成功, OS_TIMEOUT-超时
 * @note: 不能在中断里面使用
 */
int os_mutex_pend(SemaphoreHandle_t *mutex, uint32_t timeout_ms);

/* 创建软件周期定时器
 * @param p: 保留参数
 * @param func: 定时器回调函数
 * @param msec: 定时器时间，单位ms
 * @return: 定时器id, 0 表示失败(与杰理一致, 这个不是错误码)
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
 * @return: 定时器id, 0 表示失败(与杰理一致, 这个不是错误码)
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
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 
 */
int sys_timer_modify(uint32_t timer_id, uint32_t msec);

/* 重启软件定时器。重新开始计时
 * @param timer_id: 定时器id
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 
 */
int sys_timer_reset(uint32_t timer_id);

void sys_mem_dump(void);
void task_info_dump(void);


/* ======================================================================
 *                        杰理 SDK 兼容桥接层
 *
 * 上面那批是本工程自己的 OS 封装; 这一段是为"直接移植杰理(JL)代码"补的
 * 等价接口 —— 名字与语义都按杰理 SDK(os_api.h / cpu.h / spinlock.h)来。
 *
 * 当前使用方: User/ui_framework(703 点阵屏 UI 框架)。
 * 移植别的杰理模块过来时, 缺什么往这里加, 不要散落到各模块自己的适配文件里。
 * ====================================================================== */

/* ---- 信号量 / 互斥量的杰理类型名 ------------------------------------
 * 杰理代码写法是 `static OS_SEM sem; os_sem_create(&sem, 0);`
 * 本工程 os_sem_create(SemaphoreHandle_t *sem, int count) 会把新建句柄
 * 写回 *sem, 所以 OS_SEM 必须就是 SemaphoreHandle_t */
typedef SemaphoreHandle_t   OS_SEM;
typedef SemaphoreHandle_t   OS_MUTEX;

/* ---- 当前任务名 -----------------------------------------------------
 * 杰理代码常用 strcmp(os_current_task(), "xxx") 判断"我是否已经在 xxx
 * 任务上下文里", 以决定是直接做还是发消息过去。
 *
 * @return 任务名。调度器没起来、或身处中断上下文时返回空串 ——
 *         这两种情况下没有"当前任务", 返回空串可让调用方走"发消息"分支,
 *         而不是在中断里直接执行可能阻塞的操作。
 */
const char *os_current_task(void);

/* ---- 临界区 ---------------------------------------------------------
 * 杰理的 spin_lock 在单核 BR2x 上实质就是关中断; Cortex-M4 单核同理。
 * 这里用"保存/恢复 PRIMASK"实现, 支持嵌套。
 *
 * @note 只适用于【短且不阻塞】的临界区(如摘挂一个链表指针)。
 *       临界区内不要调用任何可能让出 CPU 的接口。
 */
typedef struct {
    volatile uint32_t nest;     /**< 嵌套深度, 供调试观察 */
} spinlock_t;

/** 定义并零初始化一个静态自旋锁 */
#define DEFINE_SPINLOCK(name)   spinlock_t name = { .nest = 0 }

void spin_lock_init(spinlock_t *lock);
void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);

/** 关 / 开中断, 支持嵌套(靠保存最外层的 PRIMASK 实现) */
void local_irq_disable(void);
void local_irq_enable(void);

/** 当前中断是否被屏蔽。杰理名字带前缀下划线, 保持一致 */
int  __cpu_irq_disabled(void);
int  cpu_irq_disabled(void);

/** 当前是否在中断/异常上下文里(IPSR != 0) */
int  cpu_in_irq(void);

/* 杰理用它做"中断可被解除屏蔽"的配置位, Cortex-M 无对应机制 */
#ifndef CONFIG_CPU_UNMASK_IRQ_ENABLE
#define CONFIG_CPU_UNMASK_IRQ_ENABLE    0
#endif

/* ---- 内存 -----------------------------------------------------------
 * 清零分配。杰理特有, 标准库没有。走 FreeRTOS 的 heap。 */
void *zalloc(uint32_t size);

/* ---- 看门狗 ---------------------------------------------------------
 * 杰理 SDK 里 wdt_clear 与 wdt_clr 是两个不同来源的名字, 移植过来的代码
 * 两个都在用, 所以都提供(wdt_clr 转调 wdt_clear)。
 *
 * 本工程未启用 IWDG, 目前是空实现 —— 启用后只需改 task_manager.c 里
 * wdt_clear() 一处。 */
void wdt_clear(void);
void wdt_clr(void);

/** 以 2ms 为单位的延时。杰理屏驱复位时序用的就是这个粒度 */
void delay_2ms(int cnt);

/** 杰理叫 sys_timer_re_run, 语义等同本工程的 sys_timer_reset */
#define sys_timer_re_run(id)    sys_timer_reset(id)



#endif // !__TASK_TABLE_H__

