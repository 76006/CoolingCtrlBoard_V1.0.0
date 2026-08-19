/*
 * CoolingCtrlBoard 固定 CAN 总线
 *
 * 直接打开 RT-Thread can1，接收线程把帧交给当前镜像唯一的处理函数。
 * App 用发送互斥保护状态上报和 OTA 并发；Bootloader 只有一个发送者。
 */

#include "can_bus.h"

#include <string.h>
#include <rtdevice.h>
#include <rtthread.h>

#define CAN_DEVICE_NAME          "can1"
#define CAN_BAUD_RATE            500000U
#define CAN_RX_STACK_SIZE        2048U
#define CAN_RX_PRIORITY          18U
#define CAN_RX_TIMESLICE         10U

typedef struct
{
    rt_device_t device;
    struct rt_semaphore rx_sem;
#ifndef COOLING_BOOTLOADER_BUILD
    struct rt_mutex tx_lock;
#endif
    can_bus_rx_handler_t rx_handler;
} can_bus_context_t;

static can_bus_context_t can_bus;

static rt_err_t can_bus_rx_notify(rt_device_t device, rt_size_t size)
{
    (void)device;
    (void)size;
    rt_sem_release(&can_bus.rx_sem);
    return RT_EOK;
}

static void can_bus_rx_entry(void *parameter)
{
    struct rt_can_msg message;
    can_frame_t frame;

    (void)parameter;
    while (1)
    {
        rt_sem_take(&can_bus.rx_sem, RT_WAITING_FOREVER);

        memset(&message, 0, sizeof(message));
        message.hdr_index = -1;
        while (rt_device_read(can_bus.device, 0, &message,
                              sizeof(message)) == sizeof(message))
        {
            frame.id = message.id;
            frame.format = message.ide == RT_CAN_EXTID ?
                           CAN_FRAME_FORMAT_EXTENDED :
                           CAN_FRAME_FORMAT_STANDARD;
            frame.type = message.rtr == RT_CAN_RTR ?
                         CAN_FRAME_TYPE_REMOTE : CAN_FRAME_TYPE_DATA;
            frame.dlc = (uint8_t)message.len;
            memcpy(frame.data, message.data, frame.dlc);

            can_bus.rx_handler(&frame);

            memset(&message, 0, sizeof(message));
            message.hdr_index = -1;
        }
    }
}

int can_bus_start(can_bus_rx_handler_t handler)
{
    rt_thread_t thread;

    memset(&can_bus, 0, sizeof(can_bus));
    can_bus.rx_handler = handler;
    rt_sem_init(&can_bus.rx_sem, "can_rx", 0, RT_IPC_FLAG_FIFO);
#ifndef COOLING_BOOTLOADER_BUILD
    rt_mutex_init(&can_bus.tx_lock, "can_tx", RT_IPC_FLAG_PRIO);
#endif

    can_bus.device = rt_device_find(CAN_DEVICE_NAME);
    if (can_bus.device == RT_NULL)
    {
        return -1;
    }
    if (rt_device_control(can_bus.device, RT_CAN_CMD_SET_BAUD,
                          (void *)(rt_ubase_t)CAN_BAUD_RATE) != RT_EOK)
    {
        return -1;
    }
    if (rt_device_open(can_bus.device,
                       RT_DEVICE_FLAG_INT_RX | RT_DEVICE_FLAG_INT_TX) != RT_EOK)
    {
        return -1;
    }
    if (rt_device_set_rx_indicate(can_bus.device, can_bus_rx_notify) != RT_EOK)
    {
        return -1;
    }

    thread = rt_thread_create("can_rx", can_bus_rx_entry, RT_NULL,
                              CAN_RX_STACK_SIZE, CAN_RX_PRIORITY,
                              CAN_RX_TIMESLICE);
    if (thread == RT_NULL)
    {
        return -1;
    }
    return rt_thread_startup(thread) == RT_EOK ? 0 : -1;
}

int can_bus_send(const can_frame_t *frame)
{
    struct rt_can_msg message;
    rt_size_t written;

    memset(&message, 0, sizeof(message));
    message.id = frame->id;
    message.ide = frame->format == CAN_FRAME_FORMAT_EXTENDED ?
                  RT_CAN_EXTID : RT_CAN_STDID;
    message.rtr = frame->type == CAN_FRAME_TYPE_REMOTE ?
                  RT_CAN_RTR : RT_CAN_DTR;
    message.len = frame->dlc;
    message.hdr_index = -1;
    memcpy(message.data, frame->data, frame->dlc);

#ifndef COOLING_BOOTLOADER_BUILD
    (void)rt_mutex_take(&can_bus.tx_lock, RT_WAITING_FOREVER);
#endif
    written = rt_device_write(can_bus.device, 0, &message, sizeof(message));
#ifndef COOLING_BOOTLOADER_BUILD
    rt_mutex_release(&can_bus.tx_lock);
#endif

    return written == sizeof(message) ? 0 : -1;
}
