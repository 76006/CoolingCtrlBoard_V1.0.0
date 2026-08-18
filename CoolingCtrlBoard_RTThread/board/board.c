/*
 * CoolingCtrlBoard RT-Thread 板级初始化实现
 *
 * 配置系统时钟和 RT-Thread 所需底层硬件，并在任何应用任务启动前
 * 将加热引脚置为安全关闭状态。这里还执行 UID 授权校验；授权失败时
 * 保持危险输出关闭并阻止正常应用服务进入运行阶段。
 *
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <board.h>
#include <drv_common.h>

#include "cooling_flash_layout.h"
#include "uid_encry/my_encry.h"

#ifdef RT_USING_PIN
#include <drv_gpio.h>
#endif

#ifdef RT_USING_SERIAL
#ifdef RT_USING_SERIAL_V2
#include <drv_usart_v2.h>
#else
#include <drv_usart.h>
#endif
#endif

static void cooling_board_safe_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* HTR_PWR must be low before PB0 is changed to output mode. */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);

    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = GPIO_PIN_15;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);
}

/*
 * UID 密钥无效时，在任何设备、组件或业务线程初始化之前进入静默锁定。
 * 串口和 CAN 引脚恢复为模拟输入，相关外设时钟及中断关闭，PB0 始终保持低电平。
 */
static void cooling_board_authorization_lock(void) __attribute__((noreturn));

static void cooling_board_authorization_lock(void)
{
    GPIO_InitTypeDef gpio = {0};

    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOB, &gpio);

    HAL_NVIC_DisableIRQ(USART1_IRQn);
    HAL_NVIC_DisableIRQ(CAN1_TX_IRQn);
    HAL_NVIC_DisableIRQ(CAN1_RX0_IRQn);
    HAL_NVIC_DisableIRQ(CAN1_RX1_IRQn);
    HAL_NVIC_DisableIRQ(CAN1_SCE_IRQn);

    __HAL_RCC_USART1_FORCE_RESET();
    __HAL_RCC_USART1_RELEASE_RESET();
    __HAL_RCC_USART1_CLK_DISABLE();
    __HAL_RCC_CAN1_FORCE_RESET();
    __HAL_RCC_CAN1_RELEASE_RESET();
    __HAL_RCC_CAN1_CLK_DISABLE();

    while (1)
    {
        __WFI();
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clock = {0};
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.HSIState = RCC_HSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        Error_Handler();
    }

    clock.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV2;
    clock.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }

    peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_USART1 |
                                             RCC_PERIPHCLK_ADC12;
    peripheral_clock.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
    peripheral_clock.Adc12ClockSelection = RCC_ADC12PLLCLK_DIV1;
    if (HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
    {
        Error_Handler();
    }
}

/* Override the weak common implementation so the heater output is made safe
 * before heap, device, console, or component initialization can run. */
void rt_hw_board_init(void)
{
#ifdef COOLING_BOOTLOADER_BUILD
    SCB->VTOR = COOLING_BL_START_ADDRESS;
#else
    SCB->VTOR = COOLING_APP_START_ADDRESS;
#endif
    __DSB();
    __ISB();

    HAL_Init();
    cooling_board_safe_gpio_init();

    /* 授权校验必须早于时钟、堆、串口、设备框架和组件初始化。 */
    my_encry_init();
    if (!is_encry_ok)
    {
        cooling_board_authorization_lock();
    }

    SystemClock_Config();

#ifdef RT_USING_HEAP
    rt_system_heap_init((void *)HEAP_BEGIN, (void *)HEAP_END);
#endif

#ifdef RT_USING_PIN
    rt_hw_pin_init();
#endif

#ifdef RT_USING_SERIAL
    rt_hw_usart_init();
#endif

#if defined(RT_USING_CONSOLE) && defined(RT_USING_DEVICE)
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
#endif

#if defined(RT_USING_CONSOLE) && defined(RT_USING_NANO)
    extern void rt_hw_console_init(void);
    rt_hw_console_init();
#endif

#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif
}
