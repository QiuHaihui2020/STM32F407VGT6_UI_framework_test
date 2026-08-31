#include "task_manager.h"
#include "FreeRTOS.h"
#include "string.h"
//#include "fsl_common.h"
#include <stdarg.h>
#include "stm32f4xx.h"


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
 * @return pdPASS:成功，pdFAIL:失败
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
            return pdFAIL;
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
                    return pdFAIL;
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
            return xReturn;
        }
    }
    r_printf("create task %s not found \n", name);
    configASSERT(0);
	return xReturn;
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
 * @return pdPASS 成功，pdPASS 失败
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
            return pdPASS;
        }
    }
    r_printf("kill task %s not found \n", name);
    configASSERT(0);
	return pdFALSE;
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
 * @return pdPASS 成功
 * @return pdFAIL 失败
 * @note 可以在任务或者中断里面使用
 */
BaseType_t os_taskq_post_msg(const char *name, int argc, ...)
{
    uint8_t i, j;
    UBaseType_t uxSavedInterruptStatus;//保存中断状态
    BaseType_t xReturn = pdFAIL;
    // 查找任务对应的索引
    for (i = 0; i < sizeof(task_info_table)/sizeof(task_info_table[0]); i++) {
        if (strcmp(task_info_table[i].name, name) == 0) {
            break;
        }
    }
    // 遍历数据表没有该任务时直接返回
    if (i >= sizeof(task_info_table)/sizeof(task_info_table[0])){
        return xReturn;
    }

    // 获取剩余容量
    UBaseType_t uxSpace;
    os_task_enter_critical(&uxSavedInterruptStatus);
    if (is_in_irq()) {
        uxSpace = task_info_table[i].qsize - uxQueueMessagesWaitingFromISR(*task_handle_table[i].xQueue);
    } else {
        uxSpace = uxQueueSpacesAvailable(*task_handle_table[i].xQueue);
     
    }
    if (uxSpace < (argc + 1)) {
        os_task_exit_critical(&uxSavedInterruptStatus);
        r_printf("%s task_queue_send errQUEUE_FULL\n", name);
        return errQUEUE_FULL;
    }  

    // 先发送参数个数据, 头部同时带上消息类型
    int value = argc | Q_MSG;
    if (is_in_irq()) {
        xReturn = xQueueSendFromISR(*task_handle_table[i].xQueue, &value, NULL);
    } else {
        xReturn = xQueueSend(*task_handle_table[i].xQueue, &value, 0);
    }
    if (xReturn != pdPASS) {
        os_task_exit_critical(&uxSavedInterruptStatus);
        return xReturn;
    }
    // 再发送参数列表
    va_list args;
    va_start(args, argc);  // 初始化可变参数列表，argc 是最后一个固定参数
    for (j = 0; j < argc; j++) {
        value = va_arg(args, int);
        // 区分是否中断发送
        if (is_in_irq()) {
            xReturn = xQueueSendFromISR(*task_handle_table[i].xQueue, &value, NULL);
        } else {
            xReturn = xQueueSend(*task_handle_table[i].xQueue, &value, 0);
        }
        if (xReturn != pdPASS) {
            break;
        }
    }
    va_end(args);  // 清理可变参数列表
    os_task_exit_critical(&uxSavedInterruptStatus);

    return xReturn;
}

/* 往任务消息队头发送消息
 * @param name 任务名称
 * @param argc 参数个数
 * @param ... 参数列表
 * @return pdPASS 成功
 * @return pdFAIL 失败
 * @note 可以在任务或者中断里面使用
 */
BaseType_t os_taskq_post_msg_front(const char *name, int argc, ...)
{
    int8_t i, j;
    UBaseType_t uxSavedInterruptStatus;//保存中断状态
    BaseType_t xReturn = pdFAIL;
    // 查找任务对应的索引
    for (i = 0; i < sizeof(task_info_table)/sizeof(task_info_table[0]); i++) {
        if (strcmp(task_info_table[i].name, name) == 0) {
            break;
        }
    }
    // 遍历数据表没有该任务时直接返回
    if (i >= sizeof(task_info_table)/sizeof(task_info_table[0])){
        return xReturn;
    }

    // 获取剩余容量
    UBaseType_t uxSpace;
    os_task_enter_critical(&uxSavedInterruptStatus);
    if (is_in_irq()) {
        uxSpace = task_info_table[i].qsize - uxQueueMessagesWaitingFromISR(*task_handle_table[i].xQueue);
    } else {
        uxSpace = uxQueueSpacesAvailable(*task_handle_table[i].xQueue);
     
    }
    if (uxSpace < (argc + 1)) {
        os_task_exit_critical(&uxSavedInterruptStatus);
        r_printf("%s task_queue_send errQUEUE_FULL\n", name);
        return errQUEUE_FULL;
    }  

    int value = 0;
    // 先发送参数列表
    va_list args;
    va_start(args, argc);  // 初始化可变参数列表，argc 是最后一个固定参数

    //正序读取参数并存储到数组
    int32_t *params = (int32_t *)malloc(argc * sizeof(int32_t));
    for (j = 0; j < argc; j++) {
        params[j] = va_arg(args, int);
        //r_printf("%d\n", params[j]);
    }
    //反顺序发送信号
    for (j = argc - 1; j >= 0; j--) {
        value = params[j];
        // 区分是否中断发送
        if (is_in_irq()) {
            xReturn = xQueueSendToFrontFromISR(*task_handle_table[i].xQueue, &value, NULL);
        } else {
            xReturn = xQueueSendToFront(*task_handle_table[i].xQueue, &value, 0);
        }
        if (xReturn != pdPASS) {
            break;
        }
    }
    free(params);
    va_end(args);  // 清理可变参数列表

    // 再发送参数个数, 头部同时带上消息类型
    value = argc | Q_MSG;
    if (is_in_irq()) {
        xReturn = xQueueSendToFrontFromISR(*task_handle_table[i].xQueue, &value, NULL);
    } else {
        xReturn = xQueueSendToFront(*task_handle_table[i].xQueue, &value, 0);
    }
    os_task_exit_critical(&uxSavedInterruptStatus);

    return xReturn;
}

/* 往任务消息队尾发送指定类型的消息
 * @param name 任务名称
 * @param type 消息类型 Q_MSG / Q_EVENT / Q_CALLBACK / Q_USER
 * @param argc 参数个数
 * @param argv 参数数组
 * @return pdPASS 成功
 * @return pdFAIL / errQUEUE_FULL 失败
 * @note 可以在任务或者中断里面使用
 */
BaseType_t os_taskq_post_type(const char *name, int type, int argc, int *argv)
{
    int i, j;
    UBaseType_t uxSavedInterruptStatus;//保存中断状态
    UBaseType_t uxSpace;
    BaseType_t xReturn = pdFAIL;
    int value;

    i = taskq_find_index(name);
    if (i < 0) {
        r_printf("post task %s not found \n", name);
        return xReturn;
    }
    if ((task_handle_table == NULL) || (task_handle_table[i].xQueue == NULL)) {
        r_printf("task %s queue not created \n", name);
        return xReturn;
    }
    /* 参数个数要能塞进消息头的低 20 位, 且类型不能占用参数个数的位段 */
    if ((argc < 0) || (argc > Q_ARGC_MASK) || ((argc > 0) && (argv == NULL))) {
        log_error("os_taskq_post_type argc %d err\n", argc);
        return xReturn;
    }

    // 获取剩余容量
    os_task_enter_critical(&uxSavedInterruptStatus);
    if (is_in_irq()) {
        uxSpace = task_info_table[i].qsize - uxQueueMessagesWaitingFromISR(*task_handle_table[i].xQueue);
    } else {
        uxSpace = uxQueueSpacesAvailable(*task_handle_table[i].xQueue);
    }
    if (uxSpace < (UBaseType_t)(argc + 1)) {
        os_task_exit_critical(&uxSavedInterruptStatus);
        r_printf("%s task_queue_send errQUEUE_FULL\n", name);
        return errQUEUE_FULL;
    }

    // 先发送消息头: 类型 + 参数个数
    value = (int)(((unsigned int)argc & Q_ARGC_MASK) | ((unsigned int)type & Q_TYPE_MASK));
    if (is_in_irq()) {
        xReturn = xQueueSendFromISR(*task_handle_table[i].xQueue, &value, NULL);
    } else {
        xReturn = xQueueSend(*task_handle_table[i].xQueue, &value, 0);
    }
    if (xReturn != pdPASS) {
        os_task_exit_critical(&uxSavedInterruptStatus);
        return xReturn;
    }
    // 再发送参数列表
    for (j = 0; j < argc; j++) {
        value = argv[j];
        if (is_in_irq()) {
            xReturn = xQueueSendFromISR(*task_handle_table[i].xQueue, &value, NULL);
        } else {
            xReturn = xQueueSend(*task_handle_table[i].xQueue, &value, 0);
        }
        if (xReturn != pdPASS) {
            break;
        }
    }
    os_task_exit_critical(&uxSavedInterruptStatus);

    return xReturn;
}

/* 往任务发送一个回调消息(Q_CALLBACK), 由目标任务在自己的上下文里执行 func
 * @param name 任务名称
 * @param func 回调函数, 参数只能是 int 宽度, 最多 Q_CALLBACK_ARGC_MAX 个
 * @param nargs 回调参数个数
 * @param ... 回调参数列表
 * @return pdPASS 成功, 其他为失败
 * @note 可以在任务或者中断里面使用
 */
BaseType_t os_taskq_post_callback(const char *name, void *func, int nargs, ...)
{
    int argv[Q_CALLBACK_ARGC_MAX + 2];
    int j;
    va_list args;

    if ((func == NULL) || (nargs < 0) || (nargs > Q_CALLBACK_ARGC_MAX)) {
        log_error("os_taskq_post_callback param err, nargs %d\n", nargs);
        return pdFAIL;
    }

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
 * @param argv 获取消息buf, 返回时 argv[0] 是消息头(类型|参数个数), 参数从 argv[1] 开始
 * @param argc 获取消息buf长度, 单位字节
 * @param timeout_ms 获取消息超时时间, portMAX_DELAY 为一直等待
 * @return 成功返回pdPASS，失败返回pdFAIL
 * @note 不能在中断里面使用
 * @note Q_CALLBACK 类型的消息在本函数内部直接执行掉, 不会返回给调用者,
 *       执行完继续等待下一条消息, 因此调用者只会收到 Q_MSG/Q_EVENT/Q_USER
 * @note 取消息类型用 Q_TYPE(argv[0]), 取参数个数用 Q_ARGC(argv[0])
 */
BaseType_t os_taskq_pend(int *argv, int argc, uint32_t timeout_ms) 
{
    int i, j;
    int nargs;
    int drop;
    BaseType_t xReturn = pdFAIL;
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
        return xReturn;
    }
    if ((task_handle_table == NULL) || (task_handle_table[i].xQueue == NULL)) {
        log_error("task %s queue not created \n", name);
        return xReturn;
    }

    /* 超时先换算成 tick, 之后用 xTaskCheckForTimeOut 递减:
     * Q_CALLBACK 会在本函数里被消化掉并继续等下一条消息, 如果每轮都用
     * timeout_ms 重新计时, 只要回调来得够密, 超时就永远等不到 */
    xTicksToWait = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    vTaskSetTimeOutState(&xTimeOut);

    while (1) {
        // 先取消息头
        xReturn = taskq_queue_receive(*task_handle_table[i].xQueue, &argv[0], xTicksToWait);
        if (xReturn != pdPASS) {
            return xReturn;
        }

        nargs = Q_ARGC(argv[0]);
        if ((argc / (int)sizeof(uint32_t)) < (nargs + 1)) {
            log_error("task_queue_receive: %s argc error \n", name);
            /* buf 装不下这条消息, 把它剩下的参数丢掉, 否则后面的消息会整体错位,
             * 错位的 Q_CALLBACK 会把参数当成函数指针来跑 */
            for (j = 0; j < nargs; j++) {
                taskq_queue_receive(*task_handle_table[i].xQueue, &drop, 0);
            }
            return pdFAIL;
        }

        /* 消息头和参数是在临界区里整体入队的, 到这里参数必定已经在队列中,
         * 所以取参数不需要再等待 */
        for (j = 1; j <= nargs; j++) {
            xReturn = taskq_queue_receive(*task_handle_table[i].xQueue, &argv[j], 0);
            if (xReturn != pdPASS) {
                log_error("task_queue_receive 1: %s failed \n", name);
                return xReturn;
            }
        }

        if (Q_TYPE(argv[0]) != Q_CALLBACK) {
            return pdPASS;
        }

        // Q_CALLBACK: 在当前任务上下文直接执行, 然后继续等待下一条消息
        taskq_callback_handler(&argv[1], nargs);

        if (xTaskCheckForTimeOut(&xTimeOut, &xTicksToWait) != pdFALSE) {
            return pdFAIL;
        }
    }
}

/* 清空当前任务的消息队列
 * @return pdPASS:成功，pdFAIL:失败
 * @note: 不能在中断里面使用
 */
BaseType_t task_queue_clear(void) 
{
    uint8_t i;
    //UBaseType_t uxSavedInterruptStatus;
    BaseType_t xReturn = pdFAIL;

    // 不能在中断里面使用
    configASSERT(is_in_irq() == 0, "%s can't call in irq\n", __func__); 

    //获取当前任务名字
    char *name = pcTaskGetTaskName(xTaskGetCurrentTaskHandle());
    log_debug("task_queue_clear: %s \n", name);
    // 查找任务对应的索引
    for (i = 0; i < sizeof(task_info_table)/sizeof(task_info_table[0]); i++) {
        if (strcmp(task_info_table[i].name, name) == 0) {
            break;
        }
    }

    // 遍历数据表没有该任务时直接返回
    if (i >= sizeof(task_info_table)/sizeof(task_info_table[0])){
        return xReturn;
    }

    xReturn = xQueueReset(*task_handle_table[i].xQueue);

    return xReturn;
}

/* 创建信号量
 * @param sem: 信号量句柄
 * @param count: 初始计数
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 不能在中断里面使用
 */
int os_sem_create(SemaphoreHandle_t *sem, int count)
{
    // 不能在中断里面使用
    configASSERT(is_in_irq() == 0, "%s can't call in irq\n", __func__); 

    //BaseType_t xReturn = pdFAIL;
    if (sem == NULL) {
        log_error("sem is NULL\n");
        return pdFAIL;
    }
    *sem = xSemaphoreCreateCounting(MAX_SEM_COUNT, count);
    if (*sem == NULL) {
        log_error("xSemaphoreCreateCounting failed\n");
        return pdFAIL;
    }
    return pdPASS;
}
 
/* 删除信号量，释放内存
 * @param sem: 信号量句柄
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 不能在中断里面使用
 */
int os_sem_delete(SemaphoreHandle_t *sem)
{
    // 不能在中断里面使用
    configASSERT(is_in_irq() == 0, "%s can't call in irq\n", __func__); 

    if (sem == NULL) {
        log_error("sem is NULL\n");
        return pdFAIL;
    }
    vSemaphoreDelete(*sem);
    return pdPASS;
}

/* 释放信号量
 * @param sem: 信号量句柄
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 可以在任务或者中断里面使用
 */
int os_sem_post(SemaphoreHandle_t *sem)
{
    BaseType_t xReturn = pdFAIL;
    if (sem == NULL) {
        log_error("sem is NULL\n");
        return xReturn;
    }

    if (is_in_irq()) {
        xReturn = xSemaphoreGiveFromISR(*sem, NULL);
    } else {
        xReturn = xSemaphoreGive(*sem);
    }
    return xReturn;
}

/* 获取信号量
 * @param sem: 信号量句柄
 * @param timeout_ms: 超时时间，单位ms
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 不能在中断里面使用
 */
int os_sem_pend(SemaphoreHandle_t *sem, uint32_t timeout_ms)
{
    BaseType_t xReturn = pdFAIL;
    if (sem == NULL) {
        log_error("sem is NULL\n");
        return pdFAIL;
    }

    if (is_in_irq()) {
        configASSERT(timeout_ms == 0, "%s can't call in irq\n", __func__); 
        xReturn = xSemaphoreTakeFromISR(*sem, NULL);
    } else {
        xReturn = xSemaphoreTake(*sem, (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms));
    }
    return xReturn;
}

/* 创建互斥量
 * @param mutex: 互斥量句柄
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 不能在中断里面使用
 */
int os_mutex_create(SemaphoreHandle_t *mutex)
{
    // 不能在中断里面使用
    configASSERT(is_in_irq() == 0, "%s can't call in irq\n", __func__); 

    if (mutex == NULL) {
        log_error("mutex is NULL\n");
        return pdFAIL;
    }

    *mutex = xSemaphoreCreateMutex();
    if (*mutex == NULL) {
        log_error("xSemaphoreCreateMutex failed\n");
        return pdFAIL;
    }

    return pdPASS;
}

/* 删除互斥量，释放内存
 * @param mutex: 互斥量句柄
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 不能在中断里面使用
 */
int os_mutex_delete(SemaphoreHandle_t *mutex)
{
    return os_sem_delete(mutex);
}

/* 释放互斥量
 * @param mutex: 互斥量句柄
 * @return: pdPASS-成功，pdFAIL-失败
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
 * @return: pdPASS-成功，pdFAIL-失败
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
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 
 */
int sys_timer_modify(uint32_t timer_id, uint32_t msec)
{
    if (is_in_irq()) {
        if (xTimerChangePeriodFromISR((TimerHandle_t)timer_id, pdMS_TO_TICKS(msec), NULL) != pdPASS) {
            log_error("xTimerChangePeriodFromISR failed\n");
            return pdFAIL;
        }
    } else {
        if (xTimerChangePeriod((TimerHandle_t)timer_id, pdMS_TO_TICKS(msec), 0) != pdPASS) {
            log_error("xTimerChangePeriod failed\n");
            return pdFAIL;
        }       
    }  

    return pdPASS;
}

/* 重启软件定时器。重新开始计时
 * @param timer_id: 定时器id
 * @return: pdPASS-成功，pdFAIL-失败
 * @note: 
 */
int sys_timer_reset(uint32_t timer_id)
{
   if (is_in_irq()) {
        if (xTimerResetFromISR((TimerHandle_t)timer_id, NULL) != pdPASS) {
            log_error("xTimerChangePeriodFromISR failed\n");
            return pdFAIL;
        }
    } else {
        if (xTimerReset((TimerHandle_t)timer_id, 0) != pdPASS) {
            log_error("xTimerChangePeriod failed\n");
            return pdFAIL;
        }       
    }
    return pdPASS;
}
