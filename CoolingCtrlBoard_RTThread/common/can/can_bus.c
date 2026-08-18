/*
 * CoolingCtrlBoard 统一 CAN 总线管理器实现
 *
 * 独占打开 RT-Thread CAN 设备，使用一个接收线程读取报文，再按 ID、
 * 帧格式和帧类型分发给多个订阅者；发送统一经过本模块，App 构建中
 * 还提供互斥保护和收发统计。具体驱动差异由 can_port 层隔离。
 */

#include "can_bus.h"

#include <string.h>

#include <rtthread.h>

#include "can_port.h"

#ifdef COOLING_BOOTLOADER_BUILD
#define CAN_BUS_MAX_SUBSCRIPTIONS       1U
#else
#define CAN_BUS_MAX_SUBSCRIPTIONS       8U
#endif

typedef struct
{
    can_bus_filter_t filter;
    can_bus_rx_handler_t handler;
    void *context;
} can_bus_subscription_t;

typedef struct
{
    struct rt_semaphore rx_sem;
#ifndef COOLING_BOOTLOADER_BUILD
    struct rt_mutex tx_lock;
#endif
    rt_thread_t rx_thread;
    can_bus_subscription_t subscriptions[CAN_BUS_MAX_SUBSCRIPTIONS];
#ifndef COOLING_BOOTLOADER_BUILD
    can_bus_stats_t stats;
#endif
    uint8_t subscription_count;
    uint8_t started;
} can_bus_context_t;

static can_bus_context_t g_can_bus;

static int can_bus_frame_is_valid(const can_frame_t *frame)
{
    if ((frame == RT_NULL) || (frame->dlc > CAN_FRAME_DATA_SIZE) ||
        (frame->format > CAN_FRAME_FORMAT_EXTENDED) ||
        (frame->type > CAN_FRAME_TYPE_REMOTE))
    {
        return 0;
    }

    if (frame->format == CAN_FRAME_FORMAT_STANDARD)
    {
        return frame->id <= CAN_FRAME_STD_ID_MAX;
    }
    return frame->id <= CAN_FRAME_EXT_ID_MAX;
}

static int can_bus_filter_matches(const can_bus_filter_t *filter,
                                  const can_frame_t *frame)
{
    uint8_t format_bit = (uint8_t)(1U << frame->format);
    uint8_t type_bit = (uint8_t)(1U << frame->type);

    return ((filter->format_mask & format_bit) != 0U) &&
           ((filter->type_mask & type_bit) != 0U) &&
           (((frame->id ^ filter->id) & filter->id_mask) == 0U);
}

static void can_bus_dispatch(const can_frame_t *frame)
{
    uint8_t index;
    uint8_t handled = 0U;

    for (index = 0U; index < g_can_bus.subscription_count; index++)
    {
        can_bus_subscription_t *subscription =
            &g_can_bus.subscriptions[index];

        if (!can_bus_filter_matches(&subscription->filter, frame))
        {
            continue;
        }

        if (subscription->handler(frame, subscription->context) ==
            CAN_BUS_RX_CONSUMED)
        {
            handled = 1U;
            break;
        }
    }

    if (!handled)
    {
#ifndef COOLING_BOOTLOADER_BUILD
        g_can_bus.stats.rx_unhandled++;
#else
        (void)handled;
#endif
    }
}

static void can_bus_rx_notify(void *context)
{
    (void)context;
    rt_sem_release(&g_can_bus.rx_sem);
}

static void can_bus_rx_entry(void *parameter)
{
    can_frame_t frame;
    int read_result;

    (void)parameter;
    while (1)
    {
        rt_sem_take(&g_can_bus.rx_sem, RT_WAITING_FOREVER);
        do
        {
            read_result = can_port_read(&frame);
            if (read_result > 0)
            {
#ifndef COOLING_BOOTLOADER_BUILD
                g_can_bus.stats.rx_frames++;
#endif
                if (can_bus_frame_is_valid(&frame))
                {
                    can_bus_dispatch(&frame);
                }
                else
                {
#ifndef COOLING_BOOTLOADER_BUILD
                    g_can_bus.stats.rx_errors++;
#endif
                }
            }
            else if (read_result < 0)
            {
#ifndef COOLING_BOOTLOADER_BUILD
                g_can_bus.stats.rx_errors++;
#endif
            }
        } while (read_result > 0);
    }
}

int can_bus_start(const can_bus_config_t *config)
{
    can_port_config_t port_config;
    rt_err_t result;

    if (g_can_bus.started)
    {
        return 0;
    }
    if ((config == RT_NULL) || (config->device_name == RT_NULL) ||
        (config->baud_rate == 0U) || (config->rx_thread_stack_size == 0U))
    {
        return -1;
    }

    memset(&g_can_bus, 0, sizeof(g_can_bus));
    result = rt_sem_init(&g_can_bus.rx_sem, "can_rx", 0,
                         RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        return -1;
    }
#ifndef COOLING_BOOTLOADER_BUILD
    result = rt_mutex_init(&g_can_bus.tx_lock, "can_tx", RT_IPC_FLAG_PRIO);
    if (result != RT_EOK)
    {
        rt_sem_detach(&g_can_bus.rx_sem);
        return -1;
    }
#endif

    port_config.device_name = config->device_name;
    port_config.baud_rate = config->baud_rate;
    port_config.rx_notify = can_bus_rx_notify;
    port_config.rx_context = RT_NULL;
    if (can_port_open(&port_config) != 0)
    {
#ifndef COOLING_BOOTLOADER_BUILD
        rt_mutex_detach(&g_can_bus.tx_lock);
#endif
        rt_sem_detach(&g_can_bus.rx_sem);
        return -1;
    }

    g_can_bus.rx_thread = rt_thread_create("can_rx", can_bus_rx_entry,
                                           RT_NULL,
                                           config->rx_thread_stack_size,
                                           config->rx_thread_priority,
                                           config->rx_thread_timeslice);
    if (g_can_bus.rx_thread == RT_NULL)
    {
        can_port_close();
#ifndef COOLING_BOOTLOADER_BUILD
        rt_mutex_detach(&g_can_bus.tx_lock);
#endif
        rt_sem_detach(&g_can_bus.rx_sem);
        return -1;
    }

    g_can_bus.started = 1U;
    result = rt_thread_startup(g_can_bus.rx_thread);
    if (result != RT_EOK)
    {
        g_can_bus.started = 0U;
        rt_thread_delete(g_can_bus.rx_thread);
        g_can_bus.rx_thread = RT_NULL;
        can_port_close();
#ifndef COOLING_BOOTLOADER_BUILD
        rt_mutex_detach(&g_can_bus.tx_lock);
#endif
        rt_sem_detach(&g_can_bus.rx_sem);
        return -1;
    }
    return 0;
}

int can_bus_is_started(void)
{
    return g_can_bus.started != 0U;
}

int can_bus_subscribe(const can_bus_filter_t *filter,
                      can_bus_rx_handler_t handler,
                      void *context)
{
    rt_base_t level;
    uint8_t index;

    if (!g_can_bus.started || (filter == RT_NULL) || (handler == RT_NULL) ||
        ((filter->format_mask & CAN_BUS_FORMAT_ANY_MASK) == 0U) ||
        ((filter->type_mask & CAN_BUS_TYPE_ANY_MASK) == 0U))
    {
        return -1;
    }

    level = rt_hw_interrupt_disable();
    index = g_can_bus.subscription_count;
    if (index >= CAN_BUS_MAX_SUBSCRIPTIONS)
    {
        rt_hw_interrupt_enable(level);
        return -1;
    }
    g_can_bus.subscriptions[index].filter = *filter;
    g_can_bus.subscriptions[index].handler = handler;
    g_can_bus.subscriptions[index].context = context;
    g_can_bus.subscription_count = (uint8_t)(index + 1U);
    rt_hw_interrupt_enable(level);
    return 0;
}

int can_bus_send(const can_frame_t *frame)
{
    int result;

    if (!g_can_bus.started || !can_bus_frame_is_valid(frame))
    {
        return -1;
    }
#ifndef COOLING_BOOTLOADER_BUILD
    if (rt_mutex_take(&g_can_bus.tx_lock, RT_WAITING_FOREVER) != RT_EOK)
    {
        g_can_bus.stats.tx_errors++;
        return -1;
    }
#endif

    result = can_port_write(frame);
#ifndef COOLING_BOOTLOADER_BUILD
    if (result == 0)
    {
        g_can_bus.stats.tx_frames++;
    }
    else
    {
        g_can_bus.stats.tx_errors++;
    }
    rt_mutex_release(&g_can_bus.tx_lock);
#endif
    return result;
}

void can_bus_get_stats(can_bus_stats_t *stats)
{
#ifndef COOLING_BOOTLOADER_BUILD
    rt_base_t level;
#endif

    if (stats == RT_NULL)
    {
        return;
    }
#ifdef COOLING_BOOTLOADER_BUILD
    memset(stats, 0, sizeof(*stats));
#else
    level = rt_hw_interrupt_disable();
    *stats = g_can_bus.stats;
    rt_hw_interrupt_enable(level);
#endif
}
