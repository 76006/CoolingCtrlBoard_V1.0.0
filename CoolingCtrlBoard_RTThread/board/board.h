/*
 * CoolingCtrlBoard 板级公共配置
 *
 * 定义 STM32F302CCT6 的 Flash/SRAM 容量、加热与 LS 引脚、ADC 通道
 * 映射以及 RT-Thread 堆边界。应用层通过这些逻辑名称访问硬件，避免
 * 在业务代码中散布具体 GPIO 和 ADC 通道号。
 *
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-11-5      SummerGift   first version
 */

#ifndef __BOARD_H__
#define __BOARD_H__

#include <stm32f3xx.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STM32_FLASH_START_ADRESS     ((uint32_t)0x08000000)
#define STM32_FLASH_SIZE             (256 * 1024)
#define STM32_FLASH_END_ADDRESS      ((uint32_t)(STM32_FLASH_START_ADRESS + STM32_FLASH_SIZE))

#define STM32_SRAM_SIZE              40
#define STM32_SRAM_END            (0x20000000 + STM32_SRAM_SIZE * 1024)

#define BOARD_HEATER_POWER_PIN       GET_PIN(B, 0)
#define BOARD_LIMIT_SWITCH_PIN       GET_PIN(B, 15)

/* 三路业务模拟量到 STM32 ADC1 通道的板级映射。 */
#define BOARD_NTC0_ADC_CHANNEL       1
#define BOARD_NTC1_ADC_CHANNEL       2
#define BOARD_HYSN_ADC_CHANNEL       3

#if defined(STM32F302xC)
#define CAN1                            CAN
#define CAN1_TX_IRQn                    USB_HP_CAN_TX_IRQn
#define CAN1_RX0_IRQn                   USB_LP_CAN_RX0_IRQn
#define CAN1_RX1_IRQn                   CAN_RX1_IRQn
#define CAN1_SCE_IRQn                   CAN_SCE_IRQn
#define CAN1_TX_IRQHandler              USB_HP_CAN_TX_IRQHandler
#define CAN1_RX0_IRQHandler             USB_LP_CAN_RX0_IRQHandler
#define CAN1_RX1_IRQHandler             CAN_RX1_IRQHandler
#define CAN1_SCE_IRQHandler             CAN_SCE_IRQHandler
#endif

#define BSP_ADC_SAMPLING_TIME        ADC_SAMPLETIME_181CYCLES_5

#define ADC1_CONFIG                                                     \
    {                                                                   \
        .Instance = ADC1,                                               \
        .Init =                                                         \
        {                                                               \
            .ClockPrescaler = ADC_CLOCK_ASYNC_DIV1,                     \
            .Resolution = ADC_RESOLUTION_12B,                           \
            .DataAlign = ADC_DATAALIGN_RIGHT,                           \
            .ScanConvMode = ADC_SCAN_DISABLE,                           \
            .EOCSelection = ADC_EOC_SINGLE_CONV,                        \
            .LowPowerAutoWait = DISABLE,                                \
            .ContinuousConvMode = DISABLE,                              \
            .NbrOfConversion = 1,                                       \
            .DiscontinuousConvMode = DISABLE,                           \
            .ExternalTrigConv = ADC_SOFTWARE_START,                     \
            .ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE,      \
            .DMAContinuousRequests = DISABLE,                            \
            .Overrun = ADC_OVR_DATA_OVERWRITTEN,                        \
        },                                                              \
    }

#if defined(__ARMCC_VERSION)
extern int Image$$RW_IRAM1$$ZI$$Limit;
#define HEAP_BEGIN      (&Image$$RW_IRAM1$$ZI$$Limit)
#elif __ICCARM__
#pragma section="CSTACK"
#define HEAP_BEGIN      (__segment_end("CSTACK"))
#else
extern int __bss_end;
#define HEAP_BEGIN      (&__bss_end)
#endif

#define HEAP_END        STM32_SRAM_END

void SystemClock_Config(void);

#ifdef __cplusplus
}
#endif

#endif
