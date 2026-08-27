/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOSConfig.h"
#include "log_debug.h"
#include "FreeRTOS.h"
#include "task.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;
extern DMA_HandleTypeDef hdma_i2s2_ext_rx;
extern DMA_HandleTypeDef hdma_spi2_tx;
extern DMA_HandleTypeDef hdma_usart1_tx;
extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim4;

/* USER CODE BEGIN EV */

// Parse CFSR and print specific fault causes
// #define printf log_debug   /* 与 log_debug.h 的 log_debug()->printf() 互相递归展开，会退化成对不存在的函数 log_debug() 的调用 */

// 定义RAM地址范围（根据实际MCU调整）
#define RAM_START_ADDR  0x20000000
#define RAM_SIZE        0x20000     // 128KB
#define RAM_END_ADDR    (RAM_START_ADDR + RAM_SIZE)
#define INVALID_ADDRESS 0xDEADBEEF

void Print_CFSR(uint32_t cfsr_value)
{
    // Extract sub-register values
    uint8_t mmfsr = (uint8_t)(cfsr_value & 0xFF);        // Lower 8 bits: MMFSR
    uint8_t bfsr  = (uint8_t)((cfsr_value >> 8) & 0xFF); // Middle 8 bits: BFSR
    uint16_t ufsr = (uint16_t)((cfsr_value >> 16) & 0xFFFF); // Upper 16 bits: UFSR

    // ============================
    // Parse MMFSR (Memory Management Fault Status Register)
    // ============================
    if (mmfsr != 0)
    {
        log_debug("MMFSR (Memory Management Fault Status Register) Faults:\n");
        if (mmfsr & (1 << 0)) log_debug("  - IACCVIOL: Instruction access violation\n");
        if (mmfsr & (1 << 1)) log_debug("  - DACCVIOL: Data access violation\n");
        if (mmfsr & (1 << 3)) log_debug("  - MUNSTKERR: Memory management fault on unstacking\n");
        if (mmfsr & (1 << 4)) log_debug("  - MSTKERR: Memory management fault on stacking\n");
        if (mmfsr & (1 << 5)) log_debug("  - MLSPERR: Memory management fault during floating-point lazy state preservation\n");
        if (mmfsr & (1 << 7)) log_debug("  - MMARVALID: MMFAR register holds the fault address\n");
    }

    // ============================
    // Parse BFSR (Bus Fault Status Register)
    // ============================
    if (bfsr != 0)
    {
        log_debug("BFSR (Bus Fault Status Register) Faults:\n");
        if (bfsr & (1 << 0)) log_debug("  - IBUSERR: Instruction bus error\n");
        if (bfsr & (1 << 1)) log_debug("  - PRECISERR: Precise bus error\n");
        if (bfsr & (1 << 2)) log_debug("  - IMPRECISERR: Imprecise bus error\n");
        if (bfsr & (1 << 3)) log_debug("  - UNSTKERR: Bus fault on unstacking\n");
        if (bfsr & (1 << 4)) log_debug("  - STKERR: Bus fault on stacking\n");
        if (bfsr & (1 << 5)) log_debug("  - LSPERR: Bus fault during floating-point lazy state preservation\n");
        if (bfsr & (1 << 7)) log_debug("  - BFARVALID: BFAR register holds the fault address\n");
    }

    // ============================
    // Parse UFSR (Usage Fault Status Register)
    // ============================
    if (ufsr != 0)
    {
        log_debug("UFSR (Usage Fault Status Register) Faults:\n");
        if (ufsr & (1 << 0)) log_debug("  - UNDEFINSTR: Undefined instruction\n");
        if (ufsr & (1 << 1)) log_debug("  - INVSTATE: Invalid state (e.g., illegal processor mode)\n");
        if (ufsr & (1 << 2)) log_debug("  - INVPC: Invalid PC load (e.g., jump to invalid address)\n");
        if (ufsr & (1 << 3)) log_debug("  - NOCP: No coprocessor (attempt to access non-existent coprocessor)\n");
        // 注意：根据ARM文档，这些位可能需要调整
        if (ufsr & (1 << 8)) log_debug("  - UNALIGNED: Unaligned memory access\n");
        if (ufsr & (1 << 9)) log_debug("  - DIVBYZERO: Division by zero\n");
    }
}

void Print_HFSR(uint32_t hfsr_value)
{
    log_debug("HFSR Value: 0x%08X\n", hfsr_value);

    if (hfsr_value & (1 << 31))
    {
        log_debug("  - DEBUGEVT: HardFault triggered by debug event (e.g., breakpoint)\n");
    }

    if (hfsr_value & (1 << 30))
    {
        log_debug("  - FORCED: HardFault caused by escalation from another fault (check CFSR)\n");
    }

    if (hfsr_value & (1 << 1))  // 注意：通常是bit 1，不是bit 0
    {
        log_debug("  - VECTTBL: HardFault caused by vector table read error\n");
    }

    if ((hfsr_value & 0xC0000002) == 0)  // 检查所有已知位
    {
        log_debug("  - Unknown cause (no known HFSR bits are set)\n");
    }
}

void Print_Fault_Addresses(uint32_t cfsr_value)
{
    // 检查 MMFAR 是否有效
    if (cfsr_value & (1 << 7))  // MMARVALID (bit 7)
    {
        uint32_t mmfar = SCB->MMFAR;
        log_debug("MMFAR (Memory Management Fault Address): 0x%08X\n", mmfar);
    }
    else
    {
        log_debug("MMFAR is not valid (MMARVALID=0)\n");
    }

    // 检查 BFAR 是否有效
    if ((cfsr_value >> 8) & (1 << 7))  // BFARVALID (bit 15 of CFSR)
    {
        uint32_t bfar = SCB->BFAR;
        log_debug("BFAR (Bus Fault Address): 0x%08X\n", bfar);
    }
    else
    {
        log_debug("BFAR is not valid (BFARVALID=0)\n");
    }
}
/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
    // 保存所有寄存器值（使用volatile防止优化）
    volatile uint32_t cfsr = SCB->CFSR;
    volatile uint32_t hfsr = SCB->HFSR; 
    volatile uint32_t mmfar = SCB->MMFAR;
    volatile uint32_t bfar = SCB->BFAR;
    volatile uint32_t msp = __get_MSP();
    volatile uint32_t psp = __get_PSP();
    volatile uint32_t control = __get_CONTROL();
    volatile uint32_t lr = INVALID_ADDRESS;
    volatile uint32_t pc = INVALID_ADDRESS;

    // 确定使用哪个堆栈指针
    volatile uint32_t *stack_ptr;
    if (control & (1 << 1)) {
        stack_ptr = (uint32_t *)psp;  // 任务上下文（PSP）
    } else {
        stack_ptr = (uint32_t *)msp;  // 内核/中断上下文（MSP）
    }
    
    // 安全检查堆栈指针有效性
    if (stack_ptr != NULL && 
        (uint32_t)stack_ptr >= RAM_START_ADDR && 
        (uint32_t)stack_ptr < RAM_END_ADDR &&
        ((uint32_t)stack_ptr & 0x3) == 0)  // 检查4字节对齐
    {
        // 从堆栈中读取 PC 和 LR
        pc = stack_ptr[6];  // PC 位于 SP + 24
        lr = stack_ptr[5];  // LR 位于 SP + 20
    } else {
        log_debug("Warning: Invalid stack pointer detected! SP=0x%08X\n", (uint32_t)stack_ptr);
    }
    
    // 打印故障信息
    log_debug("========== HardFault Handler ==========\n");
    log_debug("System State:\n");
    log_debug("  CONTROL: 0x%08X (PSP mode: %d)\n", control, (control >> 1) & 0x1);
    log_debug("  MSP: 0x%08X\n", msp);
    log_debug("  PSP: 0x%08X\n", psp);
    log_debug("  LR:  0x%08X\n", lr);
    log_debug("  PC:  0x%08X (Fault instruction address)\n", pc);
    
    // 打印故障状态寄存器
    log_debug("Fault Status Registers:\n");
    Print_HFSR(hfsr);
    Print_CFSR(cfsr);
    Print_Fault_Addresses(cfsr);
    
    // 判断故障发生的上下文
    if ((control >> 1) & 0x1) {
        log_debug("Fault occurred in TASK context (using PSP)\n");
    } else {
        log_debug("Fault occurred in KERNEL/ISR context (using MSP)\n");
    }
    
    // 打印完整的堆栈帧信息
    if (stack_ptr != NULL && 
        (uint32_t)stack_ptr >= RAM_START_ADDR && 
        (uint32_t)stack_ptr < RAM_END_ADDR)
    {
        log_debug("\nStack Frame at 0x%08X:\n", (uint32_t)stack_ptr);
        log_debug("  R0:  0x%08X\n", stack_ptr[0]);
        log_debug("  R1:  0x%08X\n", stack_ptr[1]);
        log_debug("  R2:  0x%08X\n", stack_ptr[2]);
        log_debug("  R3:  0x%08X\n", stack_ptr[3]);
        log_debug("  R12: 0x%08X\n", stack_ptr[4]);
        log_debug("  LR:  0x%08X\n", stack_ptr[5]);
        log_debug("  PC:  0x%08X\n", stack_ptr[6]);
        log_debug("  PSR: 0x%08X\n", stack_ptr[7]);
    }
    
    // FreeRTOS任务信息（如果可用）
#ifdef configUSE_TRACE_FACILITY
#if (configUSE_TRACE_FACILITY == 1)
    if ((control >> 1) & 0x1) {
        TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
        if (current_task != NULL) {
            log_debug("\nFreeRTOS Task Info:\n");
            log_debug("  Task Name: %s\n", pcTaskGetName(current_task));
            log_debug("  Task Priority: %d\n", (int)uxTaskPriorityGet(current_task));
#ifdef INCLUDE_uxTaskGetStackHighWaterMark
#if INCLUDE_uxTaskGetStackHighWaterMark == 1
            log_debug("  Stack High Water Mark: %d bytes\n", 
                     (int)uxTaskGetStackHighWaterMark(current_task) * sizeof(StackType_t));
#endif
#endif
        }
    }
#endif
#endif
    
    log_debug("======================================\n");
	
  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
#if !(defined vPortSVCHandler) || (vPortSVCHandler != SVC_Handler)
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVCall_IRQn 0 */
#else
  }
#endif

  /* USER CODE END SVCall_IRQn 0 */
  /* USER CODE BEGIN SVCall_IRQn 1 */

  /* USER CODE END SVCall_IRQn 1 */
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */
#if !(defined xPortPendSVHandler) || (xPortPendSVHandler != PendSV_Handler)
  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */
#endif
  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */
#if !(defined xPortSysTickHandler) || (xPortSysTickHandler != SysTick_Handler)
  /* USER CODE END PendSV_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */
#endif
  /* USER CODE END SysTick_IRQn 0 */

  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32F4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f4xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles DMA1 stream3 global interrupt.
  */
void DMA1_Stream3_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream3_IRQn 0 */

  /* USER CODE END DMA1_Stream3_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_i2s2_ext_rx);
  /* USER CODE BEGIN DMA1_Stream3_IRQn 1 */

  /* USER CODE END DMA1_Stream3_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream4 global interrupt.
  */
void DMA1_Stream4_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream4_IRQn 0 */

  /* USER CODE END DMA1_Stream4_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_spi2_tx);
  /* USER CODE BEGIN DMA1_Stream4_IRQn 1 */

  /* USER CODE END DMA1_Stream4_IRQn 1 */
}

/**
  * @brief This function handles TIM4 global interrupt.
  */
void TIM4_IRQHandler(void)
{
  /* USER CODE BEGIN TIM4_IRQn 0 */

  /* USER CODE END TIM4_IRQn 0 */
  HAL_TIM_IRQHandler(&htim4);
  /* USER CODE BEGIN TIM4_IRQn 1 */

  /* USER CODE END TIM4_IRQn 1 */
}

/**
  * @brief This function handles USART1 global interrupt.
  */
void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */

  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN USART1_IRQn 1 */

  /* USER CODE END USART1_IRQn 1 */
}

/**
  * @brief This function handles USB On The Go FS global interrupt.
  */
void OTG_FS_IRQHandler(void)
{
  /* USER CODE BEGIN OTG_FS_IRQn 0 */

  /* USER CODE END OTG_FS_IRQn 0 */
  HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
  /* USER CODE BEGIN OTG_FS_IRQn 1 */

  /* USER CODE END OTG_FS_IRQn 1 */
}

/**
  * @brief This function handles DMA2 stream7 global interrupt.
  */
void DMA2_Stream7_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream7_IRQn 0 */

  /* USER CODE END DMA2_Stream7_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart1_tx);
  /* USER CODE BEGIN DMA2_Stream7_IRQn 1 */

  /* USER CODE END DMA2_Stream7_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
