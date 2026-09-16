#include "task_manager.h"
#include "FreeRTOS.h"
#include "string.h"
//#include "fsl_common.h"
#include <stdarg.h>
#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"   /* HAL_Delay: 调度器起来之前的忙等延时 */


/*任务列表, 注意:stack_size设置为32*n*/
const struct task_info task_info_table[] = {
    {"idle_task",       1,      512,   32},
    {"app_core",        1,      512,   32},
    {"buttom_task",     1,      512,   32},
    {"power_task",      1,      512,   32},
    {"audio_task",      1,      512,   32},
    {"bt_task",         1,      512,   32},
    {"bt_trans",        1,      512,   32},
    {"lcd_task",        2,      512,   32},
    {"UartTun_task",    1,      512,   32},
    {"led_task",        1,      512,   32},
    {"Para_task",       1,      512,   32},
    {"test_task",       2,      512,   32},
    {"global_det",      3,      128,   0},
    /* 点阵屏 UI 框架的任务。名字必须是 "ui" ——
     * apps/common/ui 的 lcd_ui_api.c 里 UI_TASK_NAME 就是这个字面量,
     * task_create 与 os_taskq_post_* 都按它查表, 对不上会直接创建失败。
     *
     * 栈给 1024 字(4KB): 框架递归遍历控件树 + 字模取模 + 资源解压,
     * 比一般任务吃栈。队列 32 条: 按键/触摸/刷新消息可能短时间成串到达。 */
    {"ui",              4,     1024,   32},
};

struct task_handle *task_handle_table = NULL;

void os_task_enter_critical(UBaseType_t *uxSavedInterruptStatus)
{
    if (is_in_irq()) {
        // 进入临界区（保存当前中断状态）
        *uxSavedInterruptStatus = portSET_INTERRUPT_MASK_FROM_ISR();
    } else {
        taskENTER_CRITICAL();
    }
}

void os_task_exit_critical(UBaseType_t *uxSavedInterruptStatus)
{
    if (is_in_irq()) {
        portCLEAR_INTERRUPT_MASK_FROM_ISR(*uxSavedInterruptStatus);
    } else {
        taskEXIT_CRITICAL();
    }
}

/* 创建任务
 * @param task 任务函数
 * @param p 任务参数
 * @param name 任务名称
 * @return OS_NO_ERR(0):成功, 非 0:错误码
 */
int task_create(void (*task)(void *p), void *p, const char *name)
{
    uint8_t i = 0;
    UBaseType_t uxSavedInterruptStatus;//保存当前中断状态
    BaseType_t xReturn = pdFAIL;
    static uint8_t q_flag = 0;
    if (q_flag == 0) {
        log_debug("task_handle_table malloc %d\n", sizeof(task_info_table)/sizeof(task_info_table[0]));
        task_handle_table = malloc(sizeof(struct task_handle) * sizeof(task_info_table)/sizeof(task_info_table[0]));
        if (task_handle_table == NULL) {
            log_error("task_handle_table malloc fail\n");
            return OS_ERR_MEM_NO_MEM;
        } 
        memset(task_handle_table, 0, sizeof(struct task_handle) * sizeof(task_info_table)/sizeof(task_info_table[0]));
        q_flag = 1;
    }

    for (i = 0; i < sizeof(task_info_table)/sizeof(task_info_table[0]); i++) {
        if (strcmp(task_info_table[i].name, name) == 0) {
            // 判断队列是否创建了，没有创建则创建
            os_task_enter_critical(&uxSavedInterruptStatus);
            if (task_handle_table[i].xQueue == NULL && task_info_table[i].qsize) {
                task_handle_table[i].xQueue = malloc(sizeof(xQueueHandle));
                if (task_handle_table[i].xQueue == NULL) {
                    log_error("task_handle_table[%d].xQueue malloc fail\n", i);
                    os_task_exit_critical(&uxSavedInterruptStatus);
                    return OS_ERR_MEM_NO_MEM;
                }
                *task_handle_table[i].xQueue = xQueueCreate(task_info_table[i].qsize, sizeof(uint32_t));
            }
            if (task_handle_table[i].pxCreatedTask == NULL) {
                task_handle_table[i].pxCreatedTask = malloc(sizeof(TaskHandle_t));
                xReturn = xTaskCreate(task, 
                                    name, 
                                    task_info_table[i].stack_size, 
                                    p, 
                                    task_info_table[i].priority, 
                                    task_handle_table[i].pxCreatedTask);
            } else {
                configASSERT(0, "%s is already created", name); 
            }
            os_task_exit_critical(&uxSavedInterruptStatus);
            log_debug("task %s created, handle %p \n", name, task_handle_table[i].pxCreatedTask);
            return (xReturn == pdPASS) ? OS_NO_ERR : OS_NO_MORE_TCB;
        }
    }
    r_printf("create task %s not found \n", name);
    configASSERT(0);
	return OS_TASK_NOT_EXIST;
}

/* 启动任务调度器
 * @param void
 * @return void
 * note:
 */
void os_start(void)
{
    vTaskStartScheduler();
}

/* 删除任务
 * @param name 任务名称
 * @return OS_NO_ERR(0) 成功, OS_TASK_NOT_EXIST 任务不存在
 */
int task_kill(const char *name)
{
    int i = 0;
    UBaseType_t uxSavedInterruptStatus;//保存中断状态
    for (i = 0; i < sizeof(task_info_table)/sizeof(task_info_table[0]); i++) {
        if (strcmp(task_info_table[i].name, name) == 0 && task_handle_table[i].pxCreatedTask != NULL) {
            log_debug("task %s killed \n", name);
            os_task_enter_critical(&uxSavedInterruptStatus);
            vTaskDelete(*task_handle_table[i].pxCreatedTask);
            free(task_handle_table[i].pxCreatedTask);
            task_handle_table[i].pxCreatedTask = NULL;

            if (task_handle_table[i].xQueue) {
                vQueueDelete(*task_handle_table[i].xQueue);
                task_handle_table[i].xQueue = NULL;
            }
            os_task_exit_critical(&uxSavedInterruptStatus);
            return OS_NO_ERR;
        }
    }
    r_printf("kill task %s not found \n", name);
    configASSERT(0);
	return OS_TASK_NOT_EXIST;
}

/* 查找任务在 task_info_table 中的下标
 * @param name 任务名称
 * @return 下标, 找不到返回 -1
 */
static int taskq_find_index(const char *name)
{
    int i;
    for (i = 0; i < (int)(sizeof(task_info_table)/sizeof(task_info_table[0])); i++) {
        if (strcmp(task_info_table[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* 从队列取一个 int, 自动区分中断/任务上下文 */
static BaseType_t taskq_queue_receive(xQueueHandle xQueue, int *value, TickType_t xTicksToWait)
{
    if (is_in_irq()) {
        return xQueueReceiveFromISR(xQueue, value, NULL);
    }
    return xQueueReceive(xQueue, value, xTicksToWait);
}

/* 往队列送一个 int, 自动区分中断/任务上下文与队尾/队头 */
static BaseType_t taskq_queue_send(xQueueHandle xQueue, int value, uint8_t to_front)
{
    if (to_front) {
        if (is_in_irq()) {
            return xQueueSendToFrontFromISR(xQueue, &value, NULL);
        }
        return xQueueSendToFront(xQueue, &value, 0);
    }
    if (is_in_irq()) {
        return xQueueSendFromISR(xQueue, &value, NULL);
    }
    return xQueueSend(xQueue, &value, 0);
}

/* ----------------------------------------------------------------------
 * 消息在队列里的存放格式 —— 【唯一定义处】
 *
 *     [w0 = type] [w1 = argc] [w2 .. w(argc+1) = 参数]
 *
 * type 独占一个字, 所以是完整 32 位, 不再和 argc 挤在一起。
 *
 * @note 早先的格式是把两者塞进一个字: header = (argc & Q_ARGC_MASK) |
 *       (type & Q_TYPE_MASK)。那样 type 只剩高 12 位, 凡是用小整数当消息
 *       类型的代码(杰理 703 的 UI 框架就是, 类型取值 0~7)都会被整个抹掉,
 *       接收方永远看到 type == 0, 每条消息都派发错。改成两个字后:
 *         - 杰理风格的小整数 type 能原样收到;
 *         - Q_MSG / Q_CALLBACK 这些高位常量照旧, Q_TYPE(argv[0]) 仍然成立;
 *         - argv[0] 现在是【纯 type】, 与杰理 os_taskq_pend 的布局一致。
 *
 * @note 原先 post_msg / post_msg_front / post_type 三处各自拼了一遍消息头,
 *       现在统一走本函数, 免得格式再次漂移。
 * -------------------------------------------------------------------- */
#define TASKQ_HDR_WORDS     2

/* 一条消息最多带多少个参数。取 16 是因为最小的队列也有 32 个字,
 * 留出头部与余量; 同时避免在中断里为参数表动态分配内存 */
#define TASKQ_ARGC_MAX      16

/* 把一条完整消息(头 + 参数)整体入队
 * @param qi       任务在 task_info_table 里的下标
 * @param type     消息类型
 * @param argc     参数个数
 * @param argv     参数数组, argc 为 0 时可为 NULL
 * @param to_front 非 0 = 插到队头
 * @return OS_NO_ERR 成功, OS_Q_ERR 入队失败
 * @note 必须在临界区内调用 —— 头和参数要整体入队, 中途被别的消息插进来
 *       会让接收方整体错位, 错位的 Q_CALLBACK 会把参数当函数指针执行
 */
static int taskq_send_msg(uint8_t qi, int type, int argc,
                          const int *argv, uint8_t to_front)
{
    int j;

    /* 走到这里 taskq_send_precheck 已确认容量足够, 所以入队不该失败。
     * 真失败了说明容量检查和入队之间被打断了(调用方没进临界区), 报 OS_Q_ERR。 */
    if (to_front) {
        /* 插队头要倒着送, 送完队列里的顺序才是 type, argc, 参数... */
        for (j = argc - 1; j >= 0; j--) {
            if (taskq_queue_send(*task_handle_table[qi].xQueue, argv[j], 1) != pdPASS) {
                return OS_Q_ERR;
            }
        }
        if (taskq_queue_send(*task_handle_table[qi].xQueue, argc, 1) != pdPASS) {
            return OS_Q_ERR;
        }
        if (taskq_queue_send(*task_handle_table[qi].xQueue, type, 1) != pdPASS) {
            return OS_Q_ERR;
        }
        return OS_NO_ERR;
    }

    if (taskq_queue_send(*task_handle_table[qi].xQueue, type, 0) != pdPASS) {
        return OS_Q_ERR;
    }
    if (taskq_queue_send(*task_handle_table[qi].xQueue, argc, 0) != pdPASS) {
        return OS_Q_ERR;
    }
    for (j = 0; j < argc; j++) {
        if (taskq_queue_send(*task_handle_table[qi].xQueue, argv[j], 0) != pdPASS) {
            return OS_Q_ERR;
        }
    }
    return OS_NO_ERR;
}

/* 发送前的公共检查: 查任务下标、队列是否已建、参数个数、剩余容量
 * @param pqi 输出任务下标
 * @return OS_NO_ERR 可以发送; 其它值为具体失败原因(错误码)
 * @note 调用方需已进入临界区(容量检查到入队之间不能被打断)
 */
static int taskq_send_precheck(const char *name, int argc, const int *argv,
                               uint8_t *pqi)
{
    int i;
    UBaseType_t uxSpace;

    i = taskq_find_index(name);
    if (i < 0) {
        r_printf("post task %s not found \n", name);
        return OS_TASK_NOT_EXIST;
    }
    if ((task_handle_table == NULL) || (task_handle_table[i].xQueue == NULL)) {
        r_printf("task %s queue not created \n", name);
        return OS_ERR_NO_QBUF;
    }
    if ((argc < 0) || (argc > TASKQ_ARGC_MAX) || ((argc > 0) && (argv == NULL))) {
        log_error("taskq post argc %d err\n", argc);
        return OS_ERR_INVALID_OPT;
    }

    if (is_in_irq()) {
        uxSpace = task_info_table[i].qsize
                  - uxQueueMessagesWaitingFromISR(*task_handle_table[i].xQueue);
    } else {
        uxSpace = uxQueueSpacesAvailable(*task_handle_table[i].xQueue);
    }
    if (uxSpace < (UBaseType_t)(argc + TASKQ_HDR_WORDS)) {
        r_printf("%s task_queue_send Q_FULL\n", name);
        return OS_Q_FULL;
    }

    *pqi = (uint8_t)i;
    return OS_NO_ERR;
}

/* 执行 Q_CALLBACK 消息携带的回调
 * @param argv 消息参数, argv[0]=函数指针, argv[1]=回调参数个数, argv[2...]=回调参数
 * @param argc argv 的有效长度
 * @return 无
 * @note 这里按参数个数强转函数指针类型再调用, 与杰理 SDK 的做法一致:
 *       AAPCS 下前 4 个 int 参数走 r0-r3, 多余的走栈, 被调函数只取自己声明的
 *       那几个, 所以只要参数都是 int 宽度(含指针)就不会出错
 * @note argc==1 是杰理支持的写法(只发函数指针, 不填参数个数), 按 0 个参数处理
 */
static void taskq_callback_handler(const int *argv, int argc)
{
    void *func;
    int nargs;
    int avail;
    const int *p;

    if (argc < 1) {
        log_error("taskq callback argc %d err\n", argc);
        return;
    }
    func = (void *)argv[0];
    /* 703 里有 int msg[1] 只填函数指针就发出去的用法(见 spdif_player.c),
     * 这种消息没有参数个数字段, 当成无参回调 */
    nargs = (argc >= 2) ? argv[1] : 0;
    p = &argv[2];
    if (func == NULL) {
        log_error("taskq callback func is NULL\n");
        return;
    }
    /* 声称的参数个数不能超过实际收到的长度, 否则会把栈上的垃圾当参数传进去。
     * argc==1 时消息里连参数个数字段都没有, 可用长度按 0 算, 不能写成
     * argc-2, 那会是 -1 让下面的判断误报 */
    avail = (argc >= 2) ? (argc - 2) : 0;
    if ((nargs < 0) || (nargs > avail)) {
        log_error("taskq callback nargs %d, argc %d err\n", nargs, argc);
        return;
    }
    /* 超过支持上限时只传前 Q_CALLBACK_ARGC_MAX 个: 多传的参数被调函数不会读,
     * 少传才会出事, 所以截断比整条丢弃安全(丢弃还会漏掉参数里 malloc 的内存) */
    if (nargs > Q_CALLBACK_ARGC_MAX) {
        log_error("taskq callback nargs %d > %d, truncated\n", nargs, Q_CALLBACK_ARGC_MAX);
        nargs = Q_CALLBACK_ARGC_MAX;
    }

    switch (nargs) {
    case 0:
        ((void (*)(void))func)();
        break;
    case 1:
        ((void (*)(int))func)(p[0]);
        break;
    case 2:
        ((void (*)(int, int))func)(p[0], p[1]);
        break;
    case 3:
        ((void (*)(int, int, int))func)(p[0], p[1], p[2]);
        break;
    case 4:
        ((void (*)(int, int, int, int))func)(p[0], p[1], p[2], p[3]);
        break;
    case 5:
        ((void (*)(int, int, int, int, int))func)(p[0], p[1], p[2], p[3], p[4]);
        break;
    case 6:
        ((void (*)(int, int, int, int, int, int))func)(p[0], p[1], p[2], p[3], p[4], p[5]);
        break;
    case 7:
        ((void (*)(int, int, int, int, int, int, int))func)(p[0], p[1], p[2], p[3], p[4], p[5], p[6]);
        break;
    case 8:
        ((void (*)(int, int, int, int, int, int, int, int))func)(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        break;
    default:
        break;
    }
}

/* 往任务消息队尾发送消息
 * @param name 任务名称
 * @param argc 参数个数
 * @param ... 参数列表
 * @return OS_NO_ERR(0) 成功, 非 0 错误码
 * @note 可以在任务或者中断里面使用
 */
int os_taskq_post_msg(const char *name, int argc, ...)
{
    uint8_t qi;
    int j;
    int buf[TASKQ_ARGC_MAX];
    va_list args;
    UBaseType_t uxSavedInterruptStatus;
    int err;

    if ((argc < 0) || (argc > TASKQ_ARGC_MAX)) {
        log_error("os_taskq_post_msg argc %d err\n", argc);
        return OS_ERR_INVALID_OPT;
    }

    /* 先把可变参数收进数组, 再整体入队 —— 这样临界区里只做入队,
     * 不用在临界区内跑 va_arg */
    va_start(args, argc);
    for (j = 0; j < argc; j++) {
        buf[j] = va_arg(args, int);
    }
    va_end(args);

    os_task_enter_critical(&uxSavedInterruptStatus);
    err = taskq_send_precheck(name, argc, buf, &qi);
    if (err == OS_NO_ERR) {
        err = taskq_send_msg(qi, Q_MSG, argc, buf, 0);
    }
    os_task_exit_critical(&uxSavedInterruptStatus);

    return err;
}

/* 往任务消息队头发送消息
 * @param name 任务名称
 * @param argc 参数个数
 * @param ... 参数列表
 * @return OS_NO_ERR(0) 成功, 非 0 错误码
 * @note 可以在任务或者中断里面使用
 */
int os_taskq_post_msg_front(const char *name, int argc, ...)
{
    uint8_t qi;
    int j;
    int buf[TASKQ_ARGC_MAX];
    va_list args;
    UBaseType_t uxSavedInterruptStatus;
    int err;

    if ((argc < 0) || (argc > TASKQ_ARGC_MAX)) {
        log_error("os_taskq_post_msg_front argc %d err\n", argc);
        return OS_ERR_INVALID_OPT;
    }

    /* @note 原实现在这里 malloc 参数数组, 而本函数声明可在中断里调用 ——
     *       中断里 malloc 是不该做的事。改成栈上定长数组。 */
    va_start(args, argc);
    for (j = 0; j < argc; j++) {
        buf[j] = va_arg(args, int);
    }
    va_end(args);

    os_task_enter_critical(&uxSavedInterruptStatus);
    err = taskq_send_precheck(name, argc, buf, &qi);
    if (err == OS_NO_ERR) {
        err = taskq_send_msg(qi, Q_MSG, argc, buf, 1);
    }
    os_task_exit_critical(&uxSavedInterruptStatus);

    return err;
}

/* 往任务消息队尾发送指定类型的消息
 * @param name 任务名称
 * @param type 消息类型 Q_MSG / Q_EVENT / Q_CALLBACK / Q_USER
 * @param argc 参数个数
 * @param argv 参数数组
 * @return OS_NO_ERR(0) 成功, OS_Q_FULL 队列满, 其它见 task_manager.h 的枚举
 * @note 可以在任务或者中断里面使用
 */
int os_taskq_post_type(const char *name, int type, int argc, int *argv)
{
    uint8_t qi;
    UBaseType_t uxSavedInterruptStatus;
    int err;

    os_task_enter_critical(&uxSavedInterruptStatus);
    err = taskq_send_precheck(name, argc, argv, &qi);
    if (err == OS_NO_ERR) {
        err = taskq_send_msg(qi, type, argc, argv, 0);
    }
    os_task_exit_critical(&uxSavedInterruptStatus);

    return err;
}

/* 往任务发送一个回调消息(Q_CALLBACK), 由目标任务在自己的上下文里执行 func
 * @param name 任务名称
 * @param func 回调函数, 参数只能是 int 宽度, 最多 Q_CALLBACK_ARGC_MAX 个
 * @param nargs 回调参数个数
 * @param ... 回调参数列表
 * @return OS_NO_ERR(0) 成功, 非 0 错误码
 * @note 可以在任务或者中断里面使用
 */
int os_taskq_post_callback(const char *name, void *func, int nargs, ...)
{
    int argv[Q_CALLBACK_ARGC_MAX + 2];
    int j;
    va_list args;

    if ((func == NULL) || (nargs < 0) || (nargs > Q_CALLBACK_ARGC_MAX)) {
        log_error("os_taskq_post_callback param err, nargs %d\n", nargs);
        return OS_ERR_INVALID_OPT;
    }
    /* 成功/失败码由下面的 os_taskq_post_type 直接给出, 不用再转 */

    argv[0] = (int)func;
    argv[1] = nargs;
    va_start(args, nargs);
    for (j = 0; j < nargs; j++) {
        argv[2 + j] = va_arg(args, int);
    }
    va_end(args);

    return os_taskq_post_type(name, Q_CALLBACK, nargs + 2, argv);
}

/* @任务获取消息
 * @param argv 获取消息buf, 返回时 argv[0] 是【纯消息类型】, 参数从 argv[1] 开始
 * @param argc 获取消息buf长度, 单位字节
 * @param timeout_ms 获取消息超时时间, portMAX_DELAY 为一直等待
 * @return OS_TASKQ 取到消息; OS_TIMEOUT 超时; 其它见 task_manager.h 的枚举
 * @note 不能在中断里面使用
 * @note Q_CALLBACK 类型的消息在本函数内部直接执行掉, 不会返回给调用者,
 *       执行完继续等待下一条消息, 因此调用者只会收到 Q_MSG/Q_EVENT/Q_USER
 * @note 取消息类型用 Q_TYPE(argv[0]), 取参数个数用 Q_ARGC(argv[0])
 */
int os_taskq_pend(int *argv, int argc, uint32_t timeout_ms) 
{
    int i, j;
    int nargs;
    int drop;
    BaseType_t xReturn = pdFAIL;   /* 仅承接 FreeRTOS 返回, 不外泄 */
    TickType_t xTicksToWait;
    TimeOut_t xTimeOut;

    // 不能在中断里面使用
    configASSERT(is_in_irq() == 0, "%s can't call in irq", __func__); 

    //获取当前任务名字
    char *name = pcTaskGetTaskName(xTaskGetCurrentTaskHandle());
    // 查找任务对应的索引
    i = taskq_find_index(name);
    if (i < 0) {
        log_error("task %s not found", name);
        return OS_TASK_NOT_EXIST;
    }
    if ((task_handle_table == NULL) || (task_handle_table[i].xQueue == NULL)) {
        log_error("task %s queue not created \n", name);
        return OS_ERR_NO_QBUF;
    }

    /* 超时先换算成 tick, 之后用 xTaskCheckForTimeOut 递减:
     * Q_CALLBACK 会在本函数里被消化掉并继续等下一条消息, 如果每轮都用
     * timeout_ms 重新计时, 只要回调来得够密, 超时就永远等不到 */
    xTicksToWait = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    vTaskSetTimeOutState(&xTimeOut);

    while (1) {
        /* 消息头是两个字: w0 = type, w1 = argc。
         * argv[0] 只放 type —— 这样小整数类型不会被截断, 且布局与杰理
         * os_taskq_pend 一致(argv[0]=类型, 参数从 argv[1] 开始)。
         * 格式定义见本文件的 taskq_send_msg()。 */
        xReturn = taskq_queue_receive(*task_handle_table[i].xQueue, &argv[0], xTicksToWait);
        if (xReturn != pdPASS) {
            return OS_TIMEOUT;      /* 等不到消息 = 超时 */
        }

        /* 头的两个字是在同一个临界区里连着入队的, 所以第二个字必定已在队列中 */
        xReturn = taskq_queue_receive(*task_handle_table[i].xQueue, &nargs, 0);
        if (xReturn != pdPASS) {
            /* 头的第一个字取到了、第二个字却没有 —— 说明队列里的内容和
             * taskq_send_msg 的格式对不上了(有人绕过本模块直接往队列塞东西)。
             * 这种错乱没法就地恢复, 报 OS_Q_ERR 让调用方知道。 */
            log_error("task_queue_receive hdr: %s failed \n", name);
            return OS_Q_ERR;
        }

        if ((argc / (int)sizeof(uint32_t)) < (nargs + 1)) {
            log_error("task_queue_receive: %s argc error \n", name);
            /* buf 装不下这条消息, 把它剩下的参数丢掉, 否则后面的消息会整体错位,
             * 错位的 Q_CALLBACK 会把参数当成函数指针来跑 */
            for (j = 0; j < nargs; j++) {
                taskq_queue_receive(*task_handle_table[i].xQueue, &drop, 0);
            }
            return OS_Q_ERR;
        }

        /* 消息头和参数是在临界区里整体入队的, 到这里参数必定已经在队列中,
         * 所以取参数不需要再等待 */
        for (j = 1; j <= nargs; j++) {
            xReturn = taskq_queue_receive(*task_handle_table[i].xQueue, &argv[j], 0);
            if (xReturn != pdPASS) {
                /* 头说有 nargs 个参数, 实际取不到 —— 同上, 队列内容错乱 */
                log_error("task_queue_receive 1: %s failed \n", name);
                return OS_Q_ERR;
            }
        }

        if (Q_TYPE(argv[0]) != Q_CALLBACK) {
            return OS_TASKQ;        /* 杰理约定: 取到 taskq 消息返回 OS_TASKQ */
        }

        // Q_CALLBACK: 在当前任务上下文直接执行, 然后继续等待下一条消息
        taskq_callback_handler(&argv[1], nargs);

        if (xTaskCheckForTimeOut(&xTimeOut, &xTicksToWait) != pdFALSE) {
            return OS_TIMEOUT;
        }
    }
}

/* 清空当前任务的消息队列
 * @return OS_NO_ERR(0):成功, 非 0:错误码
 * @note: 不能在中断里面使用
 */
int task_queue_clear(void)
{
    int i;

    // 不能在中断里面使用
    configASSERT(is_in_irq() == 0, "%s can't call in irq\n", __func__);

    //获取当前任务名字
    char *name = pcTaskGetTaskName(xTaskGetCurrentTaskHandle());
    log_debug("task_queue_clear: %s \n", name);

    i = taskq_find_index(name);
    if (i < 0) {
        return OS_TASK_NOT_EXIST;
    }
    if ((task_handle_table == NULL) || (task_handle_table[i].xQueue == NULL)) {
        /* 原实现漏了这个判断, 任务在表里但队列没建(qsize=0)时会解引用
         * 空指针。补上。 */
        return OS_ERR_NO_QBUF;
    }

    /* xQueueReset 在当前 FreeRTOS 版本里恒返回 pdPASS, 但仍照实翻译 */
    return (xQueueReset(*task_handle_table[i].xQueue) == pdPASS)
           ? OS_NO_ERR : OS_Q_ERR;
}

/* 创建信号量
 * @param sem: 信号量句柄
 * @param count: 初始计数
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 不能在中断里面使用
 */
int os_sem_create(SemaphoreHandle_t *sem, int count)
{
    // 不能在中断里面使用
    configASSERT(is_in_irq() == 0, "%s can't call in irq\n", __func__); 

    if (sem == NULL) {
        log_error("sem is NULL\n");
        return OS_ERR_PEVENT_NULL;
    }
    *sem = xSemaphoreCreateCounting(MAX_SEM_COUNT, count);
    if (*sem == NULL) {
        /* 计数型信号量建不出来只可能是堆不够 */
        log_error("xSemaphoreCreateCounting failed\n");
        return OS_ERR_MEM_NO_MEM;
    }
    return OS_NO_ERR;
}
 
/* 删除信号量，释放内存
 * @param sem: 信号量句柄
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 不能在中断里面使用
 */
int os_sem_delete(SemaphoreHandle_t *sem)
{
    // 不能在中断里面使用
    configASSERT(is_in_irq() == 0, "%s can't call in irq\n", __func__); 

    if (sem == NULL) {
        log_error("sem is NULL\n");
        return OS_ERR_PEVENT_NULL;
    }
    vSemaphoreDelete(*sem);
    return OS_NO_ERR;
}

/* 释放信号量
 * @param sem: 信号量句柄
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 可以在任务或者中断里面使用
 */
int os_sem_post(SemaphoreHandle_t *sem)
{
    BaseType_t xReturn;

    if (sem == NULL) {
        log_error("sem is NULL\n");
        return OS_ERR_PEVENT_NULL;
    }

    if (is_in_irq()) {
        xReturn = xSemaphoreGiveFromISR(*sem, NULL);
    } else {
        xReturn = xSemaphoreGive(*sem);
    }

    /* 计数型信号量 Give 失败只可能是计数已达 MAX_SEM_COUNT */
    return (xReturn == pdPASS) ? OS_NO_ERR : OS_SEM_OVF;
}

/* 获取信号量
 * @param sem: 信号量句柄
 * @param timeout_ms: 超时时间，单位ms
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 不能在中断里面使用
 */
int os_sem_pend(SemaphoreHandle_t *sem, uint32_t timeout_ms)
{
    BaseType_t xReturn;
    TickType_t xTicks;

    if (sem == NULL) {
        log_error("sem is NULL\n");
        return OS_ERR_PEVENT_NULL;
    }

    if (is_in_irq()) {
        /* 中断里只能做"不等待"的尝试 */
        configASSERT(timeout_ms == 0, "%s can't call in irq\n", __func__);
        xReturn = xSemaphoreTakeFromISR(*sem, NULL);
        return (xReturn == pdPASS) ? OS_NO_ERR : OS_ERR_PEND_ISR;
    }

    /* 【杰理约定: timeout_ms == 0 表示一直等待】
     *
     * 移植过来的代码大量依赖这一点, 例如:
     *   - lcd_ui_api.c 的 lcd_ui_init() 用 os_sem_pend(&start_sem, 0)
     *     等 ui_task 初始化完成;
     *   - 703 的 dev_manager.c:1128 也是 task_create 之后紧跟
     *     os_sem_pend(&sem, 0) 的同一写法。
     *
     * 若照 FreeRTOS 原义把 0 当成"0 个 tick = 不等待", 这些地方会直接穿过去,
     * 表现为"对象还没就绪就被当成已就绪"(UI 那边就是首页显示请求被丢掉,
     * 屏幕一直空白)。 */
    if ((timeout_ms == 0) || (timeout_ms == portMAX_DELAY)) {
        xTicks = portMAX_DELAY;
    } else {
        xTicks = pdMS_TO_TICKS(timeout_ms);
    }

    xReturn = xSemaphoreTake(*sem, xTicks);
    return (xReturn == pdPASS) ? OS_NO_ERR : OS_TIMEOUT;
}

/* 创建互斥量
 * @param mutex: 互斥量句柄
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 不能在中断里面使用
 */
int os_mutex_create(SemaphoreHandle_t *mutex)
{
    // 不能在中断里面使用
    configASSERT(is_in_irq() == 0, "%s can't call in irq\n", __func__); 

    if (mutex == NULL) {
        log_error("mutex is NULL\n");
        return OS_ERR_PEVENT_NULL;
    }

    *mutex = xSemaphoreCreateMutex();
    if (*mutex == NULL) {
        log_error("xSemaphoreCreateMutex failed\n");
        return OS_ERR_MEM_NO_MEM;
    }

    return OS_NO_ERR;
}

/* 删除互斥量，释放内存
 * @param mutex: 互斥量句柄
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 不能在中断里面使用
 */
int os_mutex_delete(SemaphoreHandle_t *mutex)
{
    return os_sem_delete(mutex);
}

/* 释放互斥量
 * @param mutex: 互斥量句柄
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 不能在中断里面使用
 */
int os_mutex_post(SemaphoreHandle_t *mutex)
{
    // 不能在中断里面使用
    configASSERT(is_in_irq() == 0, "%s can't call in irq\n", __func__); 
    return os_sem_post(mutex);
}

/* 获取互斥量
 * @param mutex: 互斥量句柄
 * @param timeout_ms: 超时时间，单位ms
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 不能在中断里面使用
 */
int os_mutex_pend(SemaphoreHandle_t *mutex, uint32_t timeout_ms)
{
    // 不能在中断里面使用
    configASSERT(is_in_irq() == 0, "%s can't call in irq\n", __func__); 
    return os_sem_pend(mutex, timeout_ms);
}

/* 创建周期软件定时器
 * @param p: 保留参数
 * @param func: 定时器回调函数
 * @param msec: 定时器时间，单位ms
 * @return: 定时器id，0表示失败
 * @note: 不能在中断里面使用
 */
uint32_t sys_timer_add(void *p, void (*func)(void *priv), uint32_t msec)
{
    // 不能在中断里面使用
    configASSERT(is_in_irq() == 0, "%s can't call in irq\n", __func__); 

    if (func == NULL) {
        log_error("func is NULL\n");
        return 0;
    }

    // 创建定时器
    TimerHandle_t xTimer = xTimerCreate(NULL, pdMS_TO_TICKS(msec), pdTRUE, (void *)func, (TimerCallbackFunction_t )func);
    if (xTimer == NULL) {
        log_error("xTimerCreate failed\n");
        return 0;
    }

    if (xTimerStart(xTimer, 0) != pdPASS) {
        log_error("xTimerStart failed\n");
        return 0;
    }
    return (uint32_t)xTimer;
}

/* 删除软件定时器
 * @param timer_id: 定时器id
 * @return: 无
 * @note: 不能在中断里面使用
 */
void sys_timer_del(uint32_t timer_id)
{
    // 不能在中断里面使用
    configASSERT(is_in_irq() == 0, "%s can't call in irq\n", __func__); 

    if (xTimerDelete((TimerHandle_t)timer_id, 0) != pdPASS) {
        log_error("xTimerDelete failed\n");
    }
}

/* 添加单次软件定时器
 * @param p: 保留参数
 * @param func: 定时器回调函数
 * @param msec: 定时器时间，单位毫秒
 * @return: 定时器id，0表示失败
 * @note: 不能在中断里面使用
 */
uint32_t sys_timeout_add(void *p, void (*func)(void *priv), uint32_t msec)
{
    // 不能在中断里面使用
    configASSERT(is_in_irq() == 0, "%s can't call in irq\n", __func__); 

    if (func == NULL) {
        log_error("func is NULL\n");
        return 0;
    }

    // 创建定时器
    TimerHandle_t xTimer = xTimerCreate(NULL, pdMS_TO_TICKS(msec), pdFALSE, (void *)func, (TimerCallbackFunction_t )func);
    if (xTimer == NULL) {
        log_error("xTimerCreate1 failed\n");
        return 0;
    }

    if (xTimerStart(xTimer, 0) != pdPASS) {
        log_error("xTimerStart1 failed\n");
        return 0;
    }
    return (uint32_t)xTimer;  
}

/* 删除周期性软件定时器
 * @param timer_id: 定时器id
 * @return:
 * @note: 不能在中断里面使用
 */
void sys_timeout_del(uint32_t timer_id)
{
    sys_timer_del(timer_id);  
}

/* 修改软件定时器时间
 * @param timer_id: 定时器id
 * @param msec: 定时器时间，单位ms
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 
 */
int sys_timer_modify(uint32_t timer_id, uint32_t msec)
{
    /* 定时器命令是投递到 timer 服务任务的队列里执行的, 失败只可能是那个
     * 队列满了 —— 对应 OS_TIMER(定时器相关错误)。 */
    if (is_in_irq()) {
        if (xTimerChangePeriodFromISR((TimerHandle_t)timer_id, pdMS_TO_TICKS(msec), NULL) != pdPASS) {
            log_error("xTimerChangePeriodFromISR failed\n");
            return OS_TIMER;
        }
    } else {
        if (xTimerChangePeriod((TimerHandle_t)timer_id, pdMS_TO_TICKS(msec), 0) != pdPASS) {
            log_error("xTimerChangePeriod failed\n");
            return OS_TIMER;
        }
    }

    return OS_NO_ERR;
}

/* 重启软件定时器。重新开始计时
 * @param timer_id: 定时器id
 * @return: OS_NO_ERR(0)-成功, 非 0-错误码
 * @note: 
 */
int sys_timer_reset(uint32_t timer_id)
{
    if (is_in_irq()) {
        if (xTimerResetFromISR((TimerHandle_t)timer_id, NULL) != pdPASS) {
            log_error("xTimerResetFromISR failed\n");
            return OS_TIMER;
        }
    } else {
        if (xTimerReset((TimerHandle_t)timer_id, 0) != pdPASS) {
            log_error("xTimerReset failed\n");
            return OS_TIMER;
        }
    }
    return OS_NO_ERR;
}


/* ======================================================================
 *                        杰理 SDK 兼容桥接层
 *
 * 接口说明见 task_manager.h 末尾同名小节。
 * 当前使用方: User/ui_framework(703 点阵屏 UI 框架)。
 * ====================================================================== */

/* ---------------------------------------------------------------------- */
/* 当前任务名                                                             */
/* ---------------------------------------------------------------------- */

const char *os_current_task(void)
{
    /* 调度器没起来 / 身处中断时都没有"当前任务"。返回空串而不是 NULL:
     * 调用方普遍直接拿去 strcmp, 给 NULL 会当场崩。空串不会等于任何
     * 任务名, 正好让调用方走"发消息给目标任务"的分支。 */
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return "";
    }
    if (is_in_irq()) {
        return "";
    }
    return (const char *)pcTaskGetName(NULL);
}


/* ---------------------------------------------------------------------- */
/* 临界区 / 中断状态                                                      */
/* ---------------------------------------------------------------------- */

/** 关中断嵌套深度, 与最外层进入时保存的 PRIMASK。
 * 这两个变量只在【已关中断】的状态下被改写, 天然互斥, 无需再加保护。 */
static volatile uint32_t s_irq_nest  = 0;
static volatile uint32_t s_irq_saved = 0;

void local_irq_disable(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();

    /* 只在最外层保存原始状态 —— 内层若也保存, 就会把"进来时本来是开着的"
     * 这个事实覆盖成"关着的", 最外层 enable 时便再也开不回来 */
    if (s_irq_nest == 0) {
        s_irq_saved = primask;
    }
    s_irq_nest++;
}

void local_irq_enable(void)
{
    if (s_irq_nest == 0) {
        /* enable 比 disable 多调了一次, 属于调用方的配对错误。
         * 这里【不】恢复中断: 否则会把外层临界区提前打开, 引发的竞态
         * 比漏开中断更难查。 */
        log_error("local_irq_enable unpaired\n");
        return;
    }

    s_irq_nest--;
    if (s_irq_nest == 0) {
        __set_PRIMASK(s_irq_saved);
    }
}

int __cpu_irq_disabled(void)
{
    return (__get_PRIMASK() & 1U) ? 1 : 0;
}

int cpu_irq_disabled(void)
{
    return __cpu_irq_disabled();
}

int cpu_in_irq(void)
{
    return is_in_irq() ? 1 : 0;
}

void spin_lock_init(spinlock_t *lock)
{
    if (lock != NULL) {
        lock->nest = 0;
    }
}

void spin_lock(spinlock_t *lock)
{
    local_irq_disable();
    if (lock != NULL) {
        lock->nest++;
    }
}

void spin_unlock(spinlock_t *lock)
{
    if ((lock != NULL) && (lock->nest > 0)) {
        lock->nest--;
    }
    local_irq_enable();
}


/* ---------------------------------------------------------------------- */
/* 内存 / 看门狗 / 延时                                                   */
/* ---------------------------------------------------------------------- */

void *zalloc(uint32_t size)
{
    void *p;

    if (size == 0) {
        return NULL;
    }

    p = pvPortMalloc(size);
    if (p != NULL) {
        memset(p, 0, size);
    }
    return p;
}

void wdt_clear(void)
{
    /* 本工程未启用 IWDG。启用后在此调 HAL_IWDG_Refresh(&hiwdg) 即可 ——
     * 全工程喂狗只有这一个入口。 */
}

void wdt_clr(void)
{
    wdt_clear();
}

void delay_2ms(int cnt)
{
    if (cnt <= 0) {
        return;
    }

    /* 调度器起来之后让出 CPU; 起来之前(如上电阶段的屏初始化)只能忙等 */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)cnt * 2U));
    } else {
        HAL_Delay((uint32_t)cnt * 2U);
    }
}


