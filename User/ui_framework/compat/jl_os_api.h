/**
 * @file    jl_os_api.h
 * @brief   杰理 os_api.h 的等价物 —— 纯转发头, 不含任何实现
 *
 * 系统桥接层【全部实现在工程的 FreeRTOS/task_manager.{h,c}】里, 包括:
 *   - 任务 / 消息队列 / 信号量 / 互斥量 / 软定时器  (工程原有)
 *   - OS_SEM / OS_MUTEX 类型名
 *   - os_current_task
 *   - spinlock_t / spin_lock / spin_unlock
 *   - local_irq_disable / local_irq_enable / __cpu_irq_disabled
 *   - cpu_in_irq / cpu_irq_disabled
 *   - zalloc
 *   - wdt_clear / wdt_clr
 *   - delay_2ms
 *   - os_taskq_post_jl / os_taskq_pend_jl   (杰理语义的消息收发)
 * 见 task_manager.h 末尾的"杰理 SDK 兼容桥接层"小节。
 *
 * 为什么放在 task_manager.c 而不是本目录:
 *   这些是【项目级系统服务】, 不是 UI 框架专属 —— 以后再移植别的杰理模块
 *   过来同样要用。放在 OS 封装层里, 也避免了"UI 适配层反过来提供关中断"
 *   这种分层倒置。
 *
 * 换 RTOS 时: 改 task_manager.{h,c}, 本文件不用动。
 */
#ifndef __JL_OS_API_H__
#define __JL_OS_API_H__

#include "jl_typedef.h"
#include "task_manager.h"

#endif /* __JL_OS_API_H__ */
