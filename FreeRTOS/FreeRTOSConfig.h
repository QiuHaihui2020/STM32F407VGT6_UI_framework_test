/*
    FreeRTOS V9.0.0 - Copyright (C) 2016 Real Time Engineers Ltd.
    All rights reserved

    VISIT http://www.FreeRTOS.org TO ENSURE YOU ARE USING THE LATEST VERSION.

    This file is part of the FreeRTOS distribution.

    FreeRTOS is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License (version 2) as published by the
    Free Software Foundation >>>> AND MODIFIED BY <<<< the FreeRTOS exception.

    ***************************************************************************
    >>!   NOTE: The modification to the GPL is included to allow you to     !<<
    >>!   distribute a combined work that includes FreeRTOS without being   !<<
    >>!   obliged to provide the source code for proprietary components     !<<
    >>!   outside of the FreeRTOS kernel.                                   !<<
    ***************************************************************************

    FreeRTOS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE.  Full license text is available on the following
    link: http://www.freertos.org/a00114.html

    ***************************************************************************
     *                                                                       *
     *    FreeRTOS provides completely free yet professionally developed,    *
     *    robust, strictly quality controlled, supported, and cross          *
     *    platform software that is more than just the market leader, it     *
     *    is the industry's de facto standard.                               *
     *                                                                       *
     *    Help yourself get started quickly while simultaneously helping     *
     *    to support the FreeRTOS project by purchasing a FreeRTOS           *
     *    tutorial book, reference manual, or both:                          *
     *    http://www.FreeRTOS.org/Documentation                              *
     *                                                                       *
    ***************************************************************************

    http://www.FreeRTOS.org/FAQHelp.html - Having a problem?  Start by reading
    the FAQ page "My application does not run, what could be wrong?".  Have you
    defined configASSERT()?

    http://www.FreeRTOS.org/support - In return for receiving this top quality
    embedded software for free we request you assist our global community by
    participating in the support forum.

    http://www.FreeRTOS.org/training - Investing in training allows your team to
    be as productive as possible as early as possible.  Now you can receive
    FreeRTOS training directly from Richard Barry, CEO of Real Time Engineers
    Ltd, and the world's leading authority on the world's leading RTOS.

    http://www.FreeRTOS.org/plus - A selection of FreeRTOS ecosystem products,
    including FreeRTOS+Trace - an indispensable productivity tool, a DOS
    compatible FAT file system, and our tiny thread aware UDP/IP stack.

    http://www.FreeRTOS.org/labs - Where new FreeRTOS products go to incubate.
    Come and try FreeRTOS+TCP, our new open source TCP/IP stack for FreeRTOS.

    http://www.OpenRTOS.com - Real Time Engineers ltd. license FreeRTOS to High
    Integrity Systems ltd. to sell under the OpenRTOS brand.  Low cost OpenRTOS
    licenses offer ticketed support, indemnification and commercial middleware.

    http://www.SafeRTOS.com - High Integrity Systems also provide a safety
    engineered and independently SIL3 certified version for use in safety and
    mission critical applications that require provable dependability.

    1 tab == 4 spaces!
*/


#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*-----------------------------------------------------------
 * Application specific definitions.
 *
 * These definitions should be adjusted for your particular hardware and
 * application requirements.
 *
 * THESE PARAMETERS ARE DESCRIBED WITHIN THE 'CONFIGURATION' SECTION OF THE
 * FreeRTOS API DOCUMENTATION AVAILABLE ON THE FreeRTOS.org WEB SITE.
 *
 * See http://www.freertos.org/a00110.html.
 *----------------------------------------------------------*/

/* Prevent the inclusion of items the assembler will not understand in assembly
files. */
#if defined(__ICCARM__) || defined(__GNUC__) || defined(__CC_ARM)
/* Library includes. */
//#include <stdint.h>
#include "log_debug.h"
extern uint32_t SystemCoreClock;
#endif /* __IAR_SYSTEMS_ASM__ */

/* Normal assert() semantics without relying on the provision of an assert.h
header file. */
//extern void vAssertCalled( uint32_t ulLine, const char *pcFile );
//#define vAssertCalled( char, int ) do {r_printf( "Error: %s, line : %d\r\n", char, int );}while(1);
//#define configASSERT( x ) if( ( x ) == 0 ) vAssertCalled(  __FILE__ ,__LINE__)
#define malloc  pvPortMalloc
#define free    vPortFree

/***************************************************************************************************************/
/*                                        FreeRTOS基础配置配置选项                                              */
/***************************************************************************************************************/
/* 置1：RTOS 使用抢占式调度器，置0：RTOS 使用协作式调度器（时间片）
 *
 * 注：在多任务管理机制上，操作系统可以分为抢占式和协作式两种。
 * 协作式操作系统是任务主动释放CPU后，切换到下一个任务。
 * 任务切换的时机完全取决于正在运行的任务。
 */
#define configUSE_PREEMPTION					1
#define configUSE_TIME_SLICING                  1                               //置1：RTOS 使用时间片调度器,

/* 某些运行FreeRTOS的硬件有两种方法选择下一个要执行的任务：
 * 通用方法和特定于硬件的方法（以下简称“特殊方法”）。
 *
 * 通用方法：
 *  1.configure_PORT_OPTIMISED_TASK_SELECTION 为0 或者硬件不支持这种特殊方法。
 *  2.可以用用于所有FreeRTOS支持的硬件
 *  3.完全用C实现，效率略低于特殊方法。
 *  4.不强制要求限制最大可用优先级数目
 * 特殊方法：
 *  1.必须将configure_PORT_OPTIMISED_TASK_SELECTION设置为1。
 *  2.依赖一个或多个特定架构的汇编指令（一般是类似计算前导零[CLZ]
 * 指令）。
 *  3.比通用方法更高效
 *  4.一般强制限定最大可用优先级数目为32
 * 
 * 一般是硬件计算前导零指令，如果所使用的，MCU没有这些硬件指令的话此法该设置为0！
 */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION	1

/* The full demo always has tasks to run so the tick will never be turned off.
The blinky demo will use the default tickless idle implementation to turn the
tick off. */
#define configUSE_TICKLESS_IDLE					0                               //1启用低功耗tickless模式

#define configUSE_QUEUE_SETS					1                               //为1时启用队列
#define configCPU_CLOCK_HZ						( SystemCoreClock )             //CPU频率
#define configTICK_RATE_HZ						( 1000 )                        //系统节拍时钟频率，这里设置为1000，周期就是1ms

/* FreeRTOS 9.x 的 projdefs.h 未提供 pdTICKS_TO_MS；TinyUSB osal_freertos.h 会用到。
 * 与新版内核语义一致：毫秒 = tick * 1000 / configTICK_RATE_HZ。
 * 此处不用 TickType_t，因本文件在 FreeRTOS.h 展开顺序上早于 portmacro 对 TickType_t 的定义。 */
#ifndef pdTICKS_TO_MS
#define pdTICKS_TO_MS(xTicks) \
	((uint32_t)(((uint32_t)(xTicks) * 1000UL) / (uint32_t)configTICK_RATE_HZ))
#endif

#define configMAX_PRIORITIES					( 32 )                          //系统支持的最大优先级数目    
#define configMINIMAL_STACK_SIZE				( ( unsigned short ) 130 )      //空闲任务堆栈大小
#define configMAX_TASK_NAME_LEN					( 16 )                          //任务名字最大长度
#define configUSE_16_BIT_TICKS					0                               //系统节拍计数器类型，1表示为16位无符号整形，0表示为32位无符号整形
#define configIDLE_SHOULD_YIELD					1                               //当空闲任务处于就绪状态时，是否应让出CPU给其他任务使用
#define configUSE_TASK_NOTIFICATIONS            1                               //为1时开启任务通知功能，默认开启
#define configUSE_MUTEXES						1                               //为1时启用互斥信号量
#define configQUEUE_REGISTRY_SIZE				10                               //为0时关闭队列注册功能, 不为0时表示启用队列记录，具体的值是可以记录的队列的最大个数
#define configUSE_RECURSIVE_MUTEXES				1                               //为1时启用递归互斥信号量
#define configUSE_APPLICATION_TASK_TAG			0                               //为1时启用应用任务标签功能
#define configUSE_COUNTING_SEMAPHORES			1                               //为1时启用计数型信号量

/***************************************************************************************************************/
/*                                FreeRTOS与内存申请有关配置选项                                                */
/***************************************************************************************************************/
#define configSUPPORT_DYNAMIC_ALLOCATION        1                               //支持动态内存申请
#define configSUPPORT_STATIC_ALLOCATION         0                               //支持静态内存申请
#define configTOTAL_HEAP_SIZE					( ( size_t ) ( 64 * 1024 ) )    //系统堆大小，即系统内存大小，单位为字节

/***************************************************************************************************************/
/*                                FreeRTOS与钩子函数有关的配置选项                                              */
/***************************************************************************************************************/
#define configUSE_IDLE_HOOK						0                               //1，使用空闲钩子；0，不使用
#define configUSE_TICK_HOOK						0                               //1，使用时间片钩子；0，不使用
#define configUSE_MALLOC_FAILED_HOOK			1                               //为1时启用堆内存申请失败钩子函数
#define configCHECK_FOR_STACK_OVERFLOW			2                               //为0时关闭栈溢出检测功能，为1时启用，为2时启用高级检测方式

/***************************************************************************************************************/
/*                                FreeRTOS与运行时间和任务状态收集有关的配置选项                                 */
/***************************************************************************************************************/
/* Run time stats gathering definitions. */
#define configGENERATE_RUN_TIME_STATS	        0                              //1，启用运行时间统计功能；0，不启用
/* 以下是运行时间统计功能需要的宏定义 */
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() vConfigureTimerForRunTimeStats()
#define portGET_RUN_TIME_COUNTER_VALUE() xGetRunTimeCounterValue()
/* 可以使用SysTick作为运行时间统计的时基 */
extern void vConfigureTimerForRunTimeStats(void);
extern uint32_t xGetRunTimeCounterValue(void);

#define configUSE_TRACE_FACILITY				0                              //1，启用可视化跟踪调试工具；0，不启用

/* This demo makes use of one or more example stats formatting functions.  These
format the raw data provided by the uxTaskGetSystemState() function in to human
readable ASCII form.  See the notes in the implementation of vTaskList() within
FreeRTOS/Source/tasks.c for limitations. */
#define configUSE_STATS_FORMATTING_FUNCTIONS	1                              //1，启用格式化统计输出功能；0，不启用

/***************************************************************************************************************/
/*                                FreeRTOS与协程有关的配置选项                                                  */
/***************************************************************************************************************/
/* Co-routine definitions. */
#define configUSE_CO_ROUTINES 			        0                              //为1时启用协程，启用协程以后必须添加文件croutine.c
#define configMAX_CO_ROUTINE_PRIORITIES         ( 2 )                          //协程使用的最大优先级

/***************************************************************************************************************/
/*                                FreeRTOS与软件定时器有关的配置选项                                            */
/***************************************************************************************************************/
/* Software timer definitions. */
#define configUSE_TIMERS				        1                              //为1时启用软件定时器功能，启用软件定时器以后必须添加文件timers.c
#define configTIMER_TASK_PRIORITY		        ( configMAX_PRIORITIES - 1 )   //软件定时器任务使用的优先级
#define configTIMER_QUEUE_LENGTH		        10                              //软件定时器任务队列的长度
#define configTIMER_TASK_STACK_DEPTH	        ( configMINIMAL_STACK_SIZE * 2 )    //软件定时器任务堆栈大小

/***************************************************************************************************************/
/*                                FreeRTOS可选函数配置选项                                                      */
/***************************************************************************************************************/
/* Set the following definitions to 1 to include the API function, or zero
to exclude the API function. */
#define INCLUDE_xTaskGetSchedulerState  1       //为1时启用xTaskGetSchedulerState()函数，用于获取调度器状态
#define INCLUDE_vTaskPrioritySet		1       //为1时启用vTaskPrioritySet()函数，用于动态调整任务优先级
#define INCLUDE_uxTaskPriorityGet		1       //为1时启用uxTaskPriorityGet()函数，用于获取任务优先级
#define INCLUDE_vTaskDelete				1       //为1时启用vTaskDelete()函数，用于删除任务
#define INCLUDE_vTaskCleanUpResources	1       //为1时启用vTaskCleanUpResources()函数，用于清理任务资源
#define INCLUDE_vTaskSuspend			1       //为1时启用vTaskSuspend()函数，用于挂起任务
#define INCLUDE_vTaskDelayUntil			1       //为1时启用vTaskDelayUntil()函数，用于延时一段时间后执行任务
#define INCLUDE_vTaskDelay				1       //为1时启用vTaskDelay()函数，用于延时一段时间后执行任务
#define INCLUDE_eTaskGetState			1       //为1时启用eTaskGetState()函数，用于获取任务状态
#define INCLUDE_xTimerPendFunctionCall	1       //为1时启用xTimerPendFunctionCall()函数，用于将一个函数调用挂起，以便在定时器中断中执行

/***************************************************************************************************************/
/*                                FreeRTOS与中断有关的配置选项                                                  */
/***************************************************************************************************************/
/* Cortex-M specific definitions. */
#ifdef __NVIC_PRIO_BITS
	/* __BVIC_PRIO_BITS will be specified when CMSIS is being used. */
	#define configPRIO_BITS       		__NVIC_PRIO_BITS
#else
	#define configPRIO_BITS       		4        /* 15 priority levels */
#endif

/* The lowest interrupt priority that can be used in a call to a "set priority"
function. */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY			15          //最低中断优先级

/* The highest interrupt priority that can be used by any interrupt service
routine that makes calls to interrupt safe FreeRTOS API functions.  DO NOT CALL
INTERRUPT SAFE FREERTOS API FUNCTIONS FROM ANY INTERRUPT THAT HAS A HIGHER
PRIORITY THAN THIS! (higher priorities are lower numeric values. */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY	5           //可以调用FreeRTOS API函数的中断的最高优先级

/* Interrupt priorities used by the kernel port layer itself.  These are generic
to all Cortex-M ports, and do not rely on any particular library functions. */
#define configKERNEL_INTERRUPT_PRIORITY 		( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
/* !!!! configMAX_SYSCALL_INTERRUPT_PRIORITY must not be set to zero !!!!
See http://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html. */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 	( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

#ifndef configSTACK_DEPTH_TYPE

/* Defaults to StackType_t for backward compatibility, but can be overridden
 * in FreeRTOSConfig.h if StackType_t is too restrictive. */
    #define configSTACK_DEPTH_TYPE    StackType_t
#endif

/* Definitions that map the FreeRTOS port interrupt handlers to their CMSIS
standard names. */
#define xPortPendSVHandler PendSV_Handler
#define vPortSVCHandler SVC_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */

