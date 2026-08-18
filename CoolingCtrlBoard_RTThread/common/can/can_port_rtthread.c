/*
 * CoolingCtrlBoard CAN 端口的 RT-Thread 适配实现
 *
 * 把与操作系统相关的设备查找、波特率设置、接收通知以及 rt_can_msg
 * 收发转换封装成 can_port 接口。更换 RTOS 或 CAN 驱动时，上层总线和
 * 业务协议无需修改，只需替换该端口实现。
 */

#include "can_port.h"

#include <string.h>

#include <rtdevice.h>
#include <rtthread.h>

typedef struct
{
    rt_device_t device;
    can_port_rx_notify_t rx_notify;
    void *rx_context;
    uint8_t opened;
} can_port_context_t;

static can_port_context_t g_can_port;

static rt_err_t can_port_receive_callback(rt_device_t device, rt_size_t size)
{
    (void)device;
    (void)size;

    if (g_can_port.rx_notify != RT_NULL)
    {
        g_can_port.rx_notify(g_can_port.rx_context);
    }
    return RT_EOK;
}

int can_port_open(const can_port_config_t *config)
{
    rt_err_t result;

    if ((config == RT_NULL) || (config->device_name == RT_NULL) ||
        (config->baud_rate == 0U) || g_can_port.opened)
    {
        return -1;
    }

    memset(&g_can_port, 0, sizeof(g_can_port));
    g_can_port.device = rt_device_find(config->device_name);
    if (g_can_port.device == RT_NULL)
    {
        return -1;
    }

    result = rt_device_control(g_can_port.device, RT_CAN_CMD_SET_BAUD,
                               (void *)(rt_ubase_t)config->baud_rate);
    if (result != RT_EOK)
    {
        g_can_port.device = RT_NULL;
        return -1;
    }

    result = rt_device_open(g_can_port.device,
                            RT_DEVICE_FLAG_INT_RX | RT_DEVICE_FLAG_INT_TX);
    if (result != RT_EOK)
    {
        g_can_port.device = RT_NULL;
        return -1;
    }

    g_can_port.rx_notify = config->rx_notify;
    g_can_port.rx_context = config->rx_context;
    result = rt_device_set_rx_indicate(g_can_port.device,
                                       can_port_receive_callback);
    if (result != RT_EOK)
    {
        rt_device_close(g_can_port.device);
        memset(&g_can_port, 0, sizeof(g_can_port));
        return -1;
    }

    g_can_port.opened = 1U;
    return 0;
}

void can_port_close(void)
{
    if (!g_can_port.opened)
    {
        return;
    }

    (void)rt_device_set_rx_indicate(g_can_port.device, RT_NULL);
    (void)rt_device_close(g_can_port.device);
    memset(&g_can_port, 0, sizeof(g_can_port));
}

int can_port_read(can_frame_t *frame)
{
    struct rt_can_msg message;
    rt_size_t read_size;
    uint8_t length;

    if ((frame == RT_NULL) || !g_can_port.opened)
    {
        return -1;
    }

    memset(&message, 0, sizeof(message));
    message.hdr_index = -1;
    read_size = rt_device_read(g_can_port.device, 0, &message,
                               sizeof(message));
    if (read_size == 0U)
    {
        return 0;
    }
    if (read_size != sizeof(message))
    {
        return -1;
    }

    memset(frame, 0, sizeof(*frame));
    frame->id = message.id;
    frame->format = (message.ide == RT_CAN_EXTID) ?
                    CAN_FRAME_FORMAT_EXTENDED : CAN_FRAME_FORMAT_STANDARD;
    frame->type = (message.rtr == RT_CAN_RTR) ?
                  CAN_FRAME_TYPE_REMOTE : CAN_FRAME_TYPE_DATA;
    length = message.len > CAN_FRAME_DATA_SIZE ?
             CAN_FRAME_DATA_SIZE : (uint8_t)message.len;
    frame->dlc = length;
    memcpy(frame->data, message.data, length);
    return 1;
}

int can_port_write(const can_frame_t *frame)
{
    struct rt_can_msg message;

    if ((frame == RT_NULL) || !g_can_port.opened ||
        (frame->dlc > CAN_FRAME_DATA_SIZE))
    {
        return -1;
    }

    memset(&message, 0, sizeof(message));
    message.id = frame->id;
    message.ide = (frame->format == CAN_FRAME_FORMAT_EXTENDED) ?
                  RT_CAN_EXTID : RT_CAN_STDID;
    message.rtr = (frame->type == CAN_FRAME_TYPE_REMOTE) ?
                  RT_CAN_RTR : RT_CAN_DTR;
    message.len = frame->dlc;
    message.hdr_index = -1;
    memcpy(message.data, frame->data, frame->dlc);

    return rt_device_write(g_can_port.device, 0, &message,
                           sizeof(message)) == sizeof(message) ? 0 : -1;
}
