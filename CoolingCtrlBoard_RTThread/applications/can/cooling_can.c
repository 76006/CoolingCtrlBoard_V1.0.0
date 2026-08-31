/* CoolingCtrlBoard App CAN 命令和状态上报。 */

#include "cooling_can.h"

#include "adc/cooling_adc.h"
#include "can/can_bus.h"
#include "heater/cooling_heater.h"
#include "ota/cooling_ota.h"
#include "pressure/cooling_pressure.h"
#include "safety/cooling_safety.h"

#define CAN_DEVICE_CODE               8U
#define CAN_PROTOCOL_VERSION          10000U
#define CAN_SOFTWARE_VERSION          10000U
#define CAN_HARDWARE_VERSION          10000U
#define CAN_STATUS_PERIOD_MS          2000U
#define CAN_TEST_PERIOD_MS             500U
#define CAN_STATUS_STACK_SIZE         1024U
#define CAN_STATUS_PRIORITY           17U
#define CAN_STATUS_TIMESLICE          10U

static struct rt_semaphore can_status_wakeup;
static volatile rt_bool_t can_test_enabled = RT_FALSE;

#ifdef RT_USING_FINSH
static volatile rt_bool_t can_rx_print_enabled = RT_FALSE;
#endif

static void can_put_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)value;
}

static int can_send(uint16_t function, const uint8_t *data, uint8_t dlc)
{
    can_frame_t frame;

    frame.id = COOLING_CAN_ID(COOLING_CAN_LOCAL_NODE_ID,
                              COOLING_CAN_MASTER_NODE_ID,
                              COOLING_CAN_CLASS_NORMAL, function);
    frame.format = CAN_FRAME_FORMAT_EXTENDED;
    frame.type = CAN_FRAME_TYPE_DATA;
    frame.dlc = dlc;
    rt_memcpy(frame.data, data, dlc);
    return can_bus_send(&frame);
}

static void can_send_handshake(void)
{
    uint8_t data[COOLING_CAN_DLC_HANDSHAKE];

    can_put_u16(&data[0], CAN_DEVICE_CODE);
    can_put_u16(&data[2], CAN_PROTOCOL_VERSION);
    can_put_u16(&data[4], CAN_SOFTWARE_VERSION);
    can_put_u16(&data[6], CAN_HARDWARE_VERSION);
    (void)can_send(COOLING_CAN_FUNC_HANDSHAKE_REPLY, data, sizeof(data));
}

static uint8_t can_refrigerant_level(const cooling_pressure_snapshot_t *p)
{
    if (!p->tank_present || p->refrigerant_low)
    {
        return COOLING_CAN_REFRIGERANT_NONE;
    }
    if (p->refrigerant_sufficient)
    {
        return COOLING_CAN_REFRIGERANT_SUFFICIENT;
    }
    if (p->state == COOLING_PRESSURE_STATE_HEATING)
    {
        return COOLING_CAN_REFRIGERANT_PREHEATING;
    }
    return COOLING_CAN_REFRIGERANT_NONE;
}

static void can_send_status(void)
{
    cooling_pressure_snapshot_t pressure;
    cooling_safety_snapshot_t safety;
    uint8_t data[COOLING_CAN_DLC_STATUS] = {0};
    uint16_t pressure_value = 0U;
    uint16_t temperature = 0U;

    cooling_safety_get_snapshot(&safety);
    data[0] = safety.auto_control_enabled ?
              COOLING_CAN_AUTO_HEAT_ON : COOLING_CAN_AUTO_HEAT_OFF;

    cooling_pressure_get_snapshot(&pressure);
    data[1] = pressure.tank_present ? 1U : 0U;
    data[2] = can_refrigerant_level(&pressure);
    if (pressure.adc_valid)
    {
        (void)cooling_sensor_pressure_mpa1000(pressure.filtered,
                                              &pressure_value);
    }
    (void)cooling_sensor_temperature_deg10(&temperature);

    can_put_u16(&data[4], pressure_value);
    can_put_u16(&data[6], temperature);
    (void)can_send(COOLING_CAN_FUNC_STATUS, data, sizeof(data));
}

static void can_handle_power(const can_frame_t *frame)
{
    if (frame->data[0] == COOLING_CAN_POWER_ON)
    {
        cooling_safety_snapshot_t safety;

        cooling_safety_get_snapshot(&safety);
        if (!safety.power_on)
        {
            /* 新一轮开机默认启动自动加热；重复广播不覆盖手动关闭。 */
            cooling_safety_set_auto_control(RT_TRUE);
        }
        cooling_safety_set_power(RT_TRUE);
        rt_sem_release(&can_status_wakeup);
        can_send_handshake();
    }
    else if (frame->data[0] == COOLING_CAN_POWER_OFF)
    {
        cooling_safety_set_power(RT_FALSE);
        (void)cooling_heater_set(RT_FALSE);
        rt_sem_release(&can_status_wakeup);
    }
}

static void can_handle_auto_heat(const can_frame_t *frame)
{
    if (frame->data[0] == COOLING_CAN_AUTO_HEAT_ON)
    {
        cooling_safety_set_auto_control(RT_TRUE);
    }
    else if (frame->data[0] == COOLING_CAN_AUTO_HEAT_OFF)
    {
        cooling_safety_set_auto_control(RT_FALSE);
        (void)cooling_heater_set(RT_FALSE);
    }
}

static void can_receive(const can_frame_t *frame)
{
    uint8_t source;
    uint8_t target;
    uint8_t frame_class;
    uint16_t function;

#ifdef RT_USING_FINSH
    if (can_rx_print_enabled)
    {
        uint8_t index;

        rt_kprintf("can rx id=0x%08x %s %s dlc=%u data=",
                   (unsigned int)frame->id,
                   frame->format == CAN_FRAME_FORMAT_EXTENDED ? "ext" : "std",
                   frame->type == CAN_FRAME_TYPE_DATA ? "data" : "remote",
                   (unsigned int)frame->dlc);
        for (index = 0U; index < frame->dlc; ++index)
        {
            rt_kprintf("%02x%s", (unsigned int)frame->data[index],
                       (index + 1U < frame->dlc) ? " " : "");
        }
        rt_kprintf("\n");
    }
#endif

    if ((frame->format != CAN_FRAME_FORMAT_EXTENDED) ||
        (frame->type != CAN_FRAME_TYPE_DATA))
    {
        return;
    }

    source = COOLING_CAN_ID_SOURCE(frame->id);
    target = COOLING_CAN_ID_TARGET(frame->id);
    frame_class = COOLING_CAN_ID_CLASS(frame->id);
    function = COOLING_CAN_ID_VALUE(frame->id);

    if ((source != COOLING_CAN_MASTER_NODE_ID) ||
        ((target != COOLING_CAN_LOCAL_NODE_ID) &&
         (target != COOLING_CAN_NODE_BROADCAST)))
    {
        return;
    }

    if ((target == COOLING_CAN_LOCAL_NODE_ID) &&
        ((frame_class == COOLING_CAN_CLASS_OTA_DATA) ||
         ((frame_class == COOLING_CAN_CLASS_NORMAL) &&
          (function == COOLING_CAN_FUNC_OTA_REQUEST))))
    {
        cooling_ota_receive(frame);
        return;
    }
    if (frame_class != COOLING_CAN_CLASS_NORMAL)
    {
        return;
    }

    switch (function)
    {
    case COOLING_CAN_FUNC_POWER_BROADCAST:
        if ((target == COOLING_CAN_NODE_BROADCAST) &&
            (frame->dlc == COOLING_CAN_DLC_POWER_BROADCAST))
        {
            can_handle_power(frame);
        }
        break;
    case COOLING_CAN_FUNC_HANDSHAKE_REQ:
        if ((target == COOLING_CAN_LOCAL_NODE_ID) &&
            (frame->dlc == COOLING_CAN_DLC_HANDSHAKE))
        {
            can_send_handshake();
        }
        break;
    case COOLING_CAN_FUNC_AUTO_HEAT:
        if ((target == COOLING_CAN_LOCAL_NODE_ID) &&
            (frame->dlc == COOLING_CAN_DLC_AUTO_HEAT))
        {
            can_handle_auto_heat(frame);
        }
        break;
    default:
        break;
    }
}

static void can_status_entry(void *parameter)
{
    cooling_safety_snapshot_t safety;
    rt_tick_t last_status_tick = rt_tick_get();
    rt_tick_t last_test_tick = last_status_tick;

    (void)parameter;
    while (1)
    {
        rt_tick_t now;
        rt_uint32_t wait_ms;

        cooling_safety_get_snapshot(&safety);
        if (!safety.power_on && !can_test_enabled)
        {
            (void)rt_sem_take(&can_status_wakeup, RT_WAITING_FOREVER);
            last_status_tick = rt_tick_get();
            last_test_tick = last_status_tick;
            continue;
        }

        wait_ms = can_test_enabled ? CAN_TEST_PERIOD_MS : CAN_STATUS_PERIOD_MS;
        (void)rt_sem_take(&can_status_wakeup,
                          rt_tick_from_millisecond(wait_ms));

        now = rt_tick_get();
        if (can_test_enabled &&
            ((now - last_test_tick) >=
             rt_tick_from_millisecond(CAN_TEST_PERIOD_MS)))
        {
            can_send_handshake();
            last_test_tick = now;
        }

        cooling_safety_get_snapshot(&safety);
        if (safety.power_on)
        {
            if ((now - last_status_tick) >=
                rt_tick_from_millisecond(CAN_STATUS_PERIOD_MS))
            {
                can_send_status();
                last_status_tick = now;
            }
        }
        else
        {
            last_status_tick = now;
        }
    }
}

rt_err_t cooling_can_start(void)
{
    rt_thread_t thread;
    rt_err_t result;

    rt_sem_init(&can_status_wakeup, "canstat", 0U, RT_IPC_FLAG_FIFO);

    if (can_bus_start(can_receive) != 0)
    {
        return -RT_ERROR;
    }

    thread = rt_thread_create("canstatus", can_status_entry, RT_NULL,
                              CAN_STATUS_STACK_SIZE, CAN_STATUS_PRIORITY,
                              CAN_STATUS_TIMESLICE);
    if (thread == RT_NULL)
    {
        return -RT_ENOMEM;
    }
    result = rt_thread_startup(thread);
    if (result != RT_EOK)
    {
        rt_thread_delete(thread);
        return result;
    }

    return RT_EOK;
}

#ifdef RT_USING_FINSH
/* 每次执行 can_rx_print，反转 CAN 接收帧的持续打印状态。 */
static void can_rx_print(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    can_rx_print_enabled = can_rx_print_enabled ? RT_FALSE : RT_TRUE;
    rt_kprintf("CAN RX print %s\n",
               can_rx_print_enabled ? "on" : "off");
}
MSH_CMD_EXPORT(can_rx_print, toggle continuous CAN RX frame printing);

/* 每次执行 cantest，反转固定 CAN 握手帧的周期发送状态。 */
static void cantest(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    can_test_enabled = can_test_enabled ? RT_FALSE : RT_TRUE;
    rt_sem_release(&can_status_wakeup);
    rt_kprintf("CAN test %s, id=0x08202100 period=%ums\n",
               can_test_enabled ? "on" : "off",
               (unsigned int)CAN_TEST_PERIOD_MS);
}
MSH_CMD_EXPORT(cantest, toggle periodic CAN handshake frame transmission);
#endif
