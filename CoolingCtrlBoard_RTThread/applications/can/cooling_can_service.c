/*
 * CoolingCtrlBoard CAN 业务服务
 *
 * 在统一总线层（common/can/can_bus）之上实现扩展帧业务协议：
 *   - 接收主控发来的上/下电广播 0x1000、握手请求 0x2000、
 *     自动加热控制 0x2010；
 *   - 上电后每 2 秒上报一次状态帧 0x2110；
 *   - 只消费“已识别且来源/目标/DLC 均正确”的普通业务帧，
 *     OTA 类帧与 OTA 请求 0x2210 一律透传给 OTA 订阅者。
 *
 * 电源与自动控制状态保存在 cooling_command 模块中，与 CAN 收发解耦；
 * 加热输出的安全联锁由统一安全模块与加热模块共同保证，本服务只
 * 负责命令转发和状态发布。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_can_service.h"

#include "can/can_bus.h"
#include "can/can_codec.h"
#include "cooling_can_config.h"
#include "cooling_can_protocol.h"
#include "cooling_command.h"
#include "cooling_sensor.h"
#include "pressure/cooling_pressure.h"

/* 订阅与线程只在首次启动时创建；bus 由 can_bus 自身保证幂等。 */
static rt_uint8_t service_started;
static rt_uint8_t service_subscribed;
static rt_thread_t status_thread;
static rt_sem_t status_wakeup;

/* 组装制冷 -> 主控的普通类应答帧公共字段。 */
static void service_fill_reply_fields(cooling_can_id_fields_t *fields,
                                      uint16_t function)
{
    fields->source = COOLING_CAN_LOCAL_NODE_ID;
    fields->target = COOLING_CAN_MASTER_NODE_ID;
    fields->frame_class = COOLING_CAN_CLASS_NORMAL;
    fields->function = function;
}

static int service_send_frame(const cooling_can_id_fields_t *fields,
                              const uint8_t *data, uint8_t dlc)
{
    can_frame_t frame;
    uint32_t id;

    if ((fields == NULL) || (dlc > CAN_FRAME_DATA_SIZE) ||
        (cooling_can_make_id(fields, &id) != 0))
    {
        return -1;
    }

    rt_memset(&frame, 0, sizeof(frame));
    frame.id = id;
    frame.format = CAN_FRAME_FORMAT_EXTENDED;
    frame.type = CAN_FRAME_TYPE_DATA;
    frame.dlc = dlc;
    if ((data != NULL) && (dlc != 0U))
    {
        rt_memcpy(frame.data, data, dlc);
    }

    return can_bus_send(&frame);
}

/*
 * 握手应答 0x2100，固定 DLC8：
 * 四个 u16 依次为设备代码、协议版本、软件版本、硬件版本（大端）。
 */
static void service_send_handshake(void)
{
    cooling_can_id_fields_t fields;
    uint8_t data[COOLING_CAN_DLC_HANDSHAKE];

    service_fill_reply_fields(&fields, COOLING_CAN_FUNC_HANDSHAKE_REPLY);
    can_put_be_u16(&data[0], COOLING_CAN_DEVICE_CODE);
    can_put_be_u16(&data[2], COOLING_CAN_PROTOCOL_VERSION);
    can_put_be_u16(&data[4], COOLING_CAN_SOFTWARE_VERSION);
    can_put_be_u16(&data[6], COOLING_CAN_HARDWARE_VERSION);
    (void)service_send_frame(&fields, data, sizeof(data));
}

/* 冷媒剩余量初版离散映射（状态帧 data2）。 */
static uint8_t service_refrigerant_level(const cooling_pressure_snapshot_t *p)
{
    if ((p == NULL) || !p->tank_present)
    {
        return COOLING_CAN_REFRIGERANT_NONE;
    }

    switch (p->state)
    {
    case COOLING_PRESSURE_STATE_BOOST:
    case COOLING_PRESSURE_STATE_CONFIRM:
        /* 刚插入/升压/确认中：剩余量未知，按协议发送 255。 */
        return COOLING_CAN_REFRIGERANT_CHARGING;
    case COOLING_PRESSURE_STATE_MAINTAIN:
        return p->refrigerant_sufficient ?
               COOLING_CAN_REFRIGERANT_SUFFICIENT :
               COOLING_CAN_REFRIGERANT_NONE;
    default:
        /* 低压超时/传感器无效/未标定/联锁：按协议发送 0。 */
        return COOLING_CAN_REFRIGERANT_NONE;
    }
}

/*
 * 状态帧 0x2110，固定 DLC8：
 * data0 自动控制状态，data1 罐体存在，data2 冷媒剩余，data3 保留 0，
 * data4..5 压力（1000 = 1 MPa），data6..7 温度（1000 = 100.0 °C）。
 * 未标定或传感器无效时压力/温度编码为 0，绝不发送 ADC 原始值。
 */
static void service_send_status(void)
{
    cooling_can_id_fields_t fields;
    cooling_command_state_t command;
    cooling_pressure_snapshot_t pressure;
    uint8_t data[COOLING_CAN_DLC_STATUS];
    uint8_t auto_control = COOLING_CAN_AUTO_HEAT_ON;
    uint16_t pressure_encoded = 0U;
    uint16_t temperature_encoded = 0U;

    if (cooling_command_get(&command) == RT_EOK)
    {
        auto_control = command.auto_control ?
                       COOLING_CAN_AUTO_HEAT_ON : COOLING_CAN_AUTO_HEAT_OFF;
    }

    rt_memset(data, 0, sizeof(data));
    data[0] = auto_control;

    /* 罐体/冷媒剩余取自压力控制快照；服务未启动时保持 0。 */
    if (cooling_pressure_get_snapshot(&pressure) == RT_EOK)
    {
        data[1] = pressure.tank_present ? 1U : 0U;
        data[2] = service_refrigerant_level(&pressure);
        if (!pressure.adc_valid ||
            (cooling_sensor_pressure_mpa1000(pressure.filtered,
                                             &pressure_encoded) != 0))
        {
            pressure_encoded = 0U; /* 无效或未标定：编码 0 */
        }
    }

    /* 温度通道、ADC 有效性和换算细节全部由传感器模块封装。 */
    if (cooling_sensor_temperature_deg10(&temperature_encoded) != 0)
    {
        temperature_encoded = 0U; /* 无效或未标定：编码 0 */
    }

    can_put_be_u16(&data[4], pressure_encoded);
    can_put_be_u16(&data[6], temperature_encoded);

    service_fill_reply_fields(&fields, COOLING_CAN_FUNC_STATUS);
    (void)service_send_frame(&fields, data, sizeof(data));
}

/* 上/下电广播：目标广播、DLC4，data0 = 1 上电 / 0 下电。 */
static can_bus_rx_result_t service_handle_power(const can_frame_t *frame)
{
    rt_uint8_t command = frame->data[0];

    if (command == COOLING_CAN_POWER_ON)
    {
        /* 先锁存上电，再回复握手；状态线程此后每 2 秒上报一次。 */
        (void)cooling_command_set_power(RT_TRUE);
        if (status_wakeup != RT_NULL)
        {
            rt_sem_release(status_wakeup);
        }
        service_send_handshake();
    }
    else if (command == COOLING_CAN_POWER_OFF)
    {
        /* 命令模块负责发布安全状态并立即关闭加热。 */
        (void)cooling_command_set_power(RT_FALSE);
        if (status_wakeup != RT_NULL)
        {
            rt_sem_release(status_wakeup);
        }
    }
    else
    {
        return CAN_BUS_RX_PASS; /* 取值非法，不消费 */
    }

    return CAN_BUS_RX_CONSUMED;
}

/* 自动加热控制：DLC2，data0 = 1 启用 / 0 禁用。 */
static can_bus_rx_result_t service_handle_auto_heat(const can_frame_t *frame)
{
    rt_uint8_t command = frame->data[0];
    rt_bool_t enabled;

    if (command == COOLING_CAN_AUTO_HEAT_ON)
    {
        enabled = RT_TRUE;
    }
    else if (command == COOLING_CAN_AUTO_HEAT_OFF)
    {
        enabled = RT_FALSE;
    }
    else
    {
        return CAN_BUS_RX_PASS; /* 取值非法，不消费 */
    }

    (void)cooling_command_set_auto_control(enabled);

    return CAN_BUS_RX_CONSUMED;
}

/*
 * 业务帧分发：过滤扩展数据帧，只消费已识别且来源/目标/DLC 正确的
 * 普通业务帧；OTA 类帧与 OTA 请求必须透传给后注册的 OTA 订阅者。
 * 本回调运行在 can_bus 接收线程内，不是中断上下文。
 */
static can_bus_rx_result_t service_rx(const can_frame_t *frame, void *context)
{
    cooling_can_id_fields_t fields;

    (void)context;

    if ((frame->format != CAN_FRAME_FORMAT_EXTENDED) ||
        (frame->type != CAN_FRAME_TYPE_DATA))
    {
        return CAN_BUS_RX_PASS;
    }
    if (cooling_can_parse_id(frame->id, &fields) != 0)
    {
        return CAN_BUS_RX_PASS;
    }

    /* OTA 类帧必须透传，业务服务绝不消费。 */
    if (fields.frame_class != COOLING_CAN_CLASS_NORMAL)
    {
        return CAN_BUS_RX_PASS;
    }

    /* 普通业务帧只认“主控来源 + 本机/广播目标”。 */
    if ((fields.source != COOLING_CAN_MASTER_NODE_ID) ||
        !cooling_can_is_for_node(&fields, COOLING_CAN_LOCAL_NODE_ID))
    {
        return CAN_BUS_RX_PASS;
    }

    switch (fields.function)
    {
    case COOLING_CAN_FUNC_POWER_BROADCAST:
        /* 上/下电是广播帧：目标必须为广播且 DLC 固定 4。 */
        if ((fields.target != COOLING_CAN_NODE_BROADCAST) ||
            (frame->dlc != COOLING_CAN_DLC_POWER_BROADCAST))
        {
            return CAN_BUS_RX_PASS;
        }
        return service_handle_power(frame);

    case COOLING_CAN_FUNC_HANDSHAKE_REQ:
        if ((fields.target != COOLING_CAN_LOCAL_NODE_ID) ||
            (frame->dlc != COOLING_CAN_DLC_HANDSHAKE))
        {
            return CAN_BUS_RX_PASS;
        }
        /* 收到主控握手请求，回复制冷握手。 */
        service_send_handshake();
        return CAN_BUS_RX_CONSUMED;

    case COOLING_CAN_FUNC_AUTO_HEAT:
        if ((fields.target != COOLING_CAN_LOCAL_NODE_ID) ||
            (frame->dlc != COOLING_CAN_DLC_AUTO_HEAT))
        {
            return CAN_BUS_RX_PASS;
        }
        return service_handle_auto_heat(frame);

    case COOLING_CAN_FUNC_OTA_REQUEST:
        /* OTA 请求属于 OTA 订阅者的业务，透传。 */
        return CAN_BUS_RX_PASS;

    default:
        /* 未识别的业务帧不消费，由总线层计入 unhandled。 */
        return CAN_BUS_RX_PASS;
    }
}

/*
 * 周期状态线程：下电时阻塞等待，上电事件到达后从零开始计时；
 * 下电事件会打断当前周期，保证关闭期间不再发送状态。
 */
static void service_status_entry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        cooling_command_state_t command;
        rt_err_t wait_result;

        if ((cooling_command_get(&command) != RT_EOK) || !command.power_on)
        {
            (void)rt_sem_take(status_wakeup, RT_WAITING_FOREVER);
            continue;
        }

        wait_result = rt_sem_take(
            status_wakeup,
            rt_tick_from_millisecond(COOLING_CAN_STATUS_PERIOD_MS));
        if (wait_result == -RT_ETIMEOUT)
        {
            /* 超时后复查电源，避免与刚到达的下电命令交叉发送。 */
            if ((cooling_command_get(&command) == RT_EOK) && command.power_on)
            {
                service_send_status();
            }
        }
    }
}

rt_err_t cooling_can_service_start(void)
{
    static const can_bus_config_t config =
    {
        COOLING_CAN_DEVICE_NAME,
        COOLING_CAN_BAUD_RATE,
        COOLING_CAN_RX_THREAD_STACK_SIZE,
        COOLING_CAN_RX_THREAD_PRIORITY,
        COOLING_CAN_RX_THREAD_TIMESLICE
    };
    can_bus_filter_t filter;
    rt_err_t result;

    /* 重复启动幂等。 */
    if (service_started)
    {
        return RT_EOK;
    }

    /* 命令状态模块必须先可用，业务帧处理与状态上报都依赖它。 */
    if (cooling_command_init() != RT_EOK)
    {
        return -RT_ENOMEM;
    }

    if (can_bus_start(&config) != 0)
    {
        return -RT_ERROR;
    }

    if (status_wakeup == RT_NULL)
    {
        status_wakeup = rt_sem_create("canstat", 0U, RT_IPC_FLAG_FIFO);
        if (status_wakeup == RT_NULL)
        {
            return -RT_ENOMEM;
        }
    }

    /* 只注册一次订阅：所有扩展数据帧，仅消费已识别的普通业务帧。 */
    if (!service_subscribed)
    {
        filter.id = 0U;
        filter.id_mask = 0U;
        filter.format_mask = CAN_BUS_FORMAT_EXTENDED_MASK;
        filter.type_mask = CAN_BUS_TYPE_DATA_MASK;
        if (can_bus_subscribe(&filter, service_rx, RT_NULL) != 0)
        {
            return -RT_ERROR;
        }
        service_subscribed = 1U;
    }

    /* 只创建一个周期状态线程。 */
    if (status_thread == RT_NULL)
    {
        status_thread = rt_thread_create("canstatus",
                                         service_status_entry,
                                         RT_NULL,
                                         COOLING_CAN_STATUS_THREAD_STACK_SIZE,
                                         COOLING_CAN_STATUS_THREAD_PRIORITY,
                                         COOLING_CAN_STATUS_THREAD_TIMESLICE);
        if (status_thread == RT_NULL)
        {
            return -RT_ENOMEM;
        }

        result = rt_thread_startup(status_thread);
        if (result != RT_EOK)
        {
            rt_thread_delete(status_thread);
            status_thread = RT_NULL;
            return result;
        }
    }

    service_started = 1U;
    return RT_EOK;
}

#ifdef RT_USING_FINSH
/* 调试命令：输入 can_stats 查看统一总线层收发统计。 */
static void can_stats(int argc, char **argv)
{
    can_bus_stats_t stats;

    (void)argc;
    (void)argv;
    can_bus_get_stats(&stats);
    rt_kprintf("can rx=%u unhandled=%u rxerr=%u tx=%u txerr=%u\n",
               (unsigned int)stats.rx_frames,
               (unsigned int)stats.rx_unhandled,
               (unsigned int)stats.rx_errors,
               (unsigned int)stats.tx_frames,
               (unsigned int)stats.tx_errors);
}
MSH_CMD_EXPORT(can_stats, show shared CAN bus statistics);
#endif
