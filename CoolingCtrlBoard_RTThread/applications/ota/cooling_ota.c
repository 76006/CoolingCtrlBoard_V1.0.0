/*
 * CoolingCtrlBoard App 扩展帧 OTA 接收服务（工作线程模型）
 *
 * 替代早期基于标准 ID 的 common/fota_can 方案（该文件保留，但本应用
 * 不再启动或查询它）。本实现接收 cooling_can 分发的扩展数据帧：
 *   - 0x2210 普通类请求帧（主控 -> 制冷，DLC8：版本 u16、整包 CRC16、
 *     RBL 总字节数 u32）；
 *   - 类别 1 的 OTA 数据帧，ID 低 16 位为 8 字节帧序号，offset = seq*8，
 *     最后一帧 DLC 为实际剩余长度。
 *
 * 线程模型：CAN 接收入口只做帧分类与入队（队列满时请求回
 * 准备失败、数据回该序号 ACK 失败），禁止在回调内执行 fal_partition_
 * erase/write、fota_meta_set、fota_rbl_verify、CRC 全量计算、长延时
 * 或复位。全部 Flash/校验/元数据操作由 OTA 工作线程串行执行，因此
 * 传输状态（expected_seq、received_bytes、CRC16、last_activity）只有
 * 工作线程一个写者，不再需要 ota_lock。
 *
 * 流程：请求校验 -> 置忙 + 安全 OTA 联锁 + 关加热 -> 写 DOWNLOADING
 * 元数据 + 擦除下载区 -> 准备应答 -> 按序号收数据（逐帧 ACK，1000 ms
 * 帧间超时由工作线程带超时接收队列实现，仅 RECEIVING 状态计时）
 * -> 收满后先整包 CRC16-CCITT-FALSE、再 fota_rbl_verify 两级校验
 * -> 写 READY 元数据（rbl_crc 保存整区 CRC32 供 Bootloader 复验）
 * -> 完成帧 -> 短延时 -> 复位。成功保持忙态与安全 OTA 联锁直到复位。
 *
 * OTA 期间绝不主动发送 0x2200 错误帧。
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cooling_ota.h"

#include <board.h>

#include "can/can_bus.h"
#include "can/cooling_can.h"
#include "cooling_flash_layout.h"
#include "cooling_ota_config.h"
#include "fal.h"
#include "fota_meta.h"
#include "fota_rbl.h"
#include "fota_storage.h"
#include "heater/cooling_heater.h"
#include "safety/cooling_safety.h"

/* 接收状态机（只有 OTA 工作线程写，其他线程不读）。 */
typedef enum
{
    COOLING_OTA_RX_IDLE = 0,      /* 空闲，可接受新请求 */
    COOLING_OTA_RX_PREPARING,     /* 已置忙：写元数据并擦除下载区 */
    COOLING_OTA_RX_RECEIVING,     /* 已回复准备=1：按序号接收数据 */
    COOLING_OTA_RX_VERIFYING      /* 数据收满：执行两级校验 */
} cooling_ota_rx_state_t;

/* 消息队列消息：区分请求帧与数据帧，携带一份 can_frame_t 拷贝。 */
typedef enum
{
    COOLING_OTA_MSG_REQUEST = 0,
    COOLING_OTA_MSG_DATA = 1
} cooling_ota_msg_type_t;

typedef struct
{
    cooling_ota_msg_type_t type;
    can_frame_t frame;
} cooling_ota_msg_t;

static void ota_handle_request(const can_frame_t *frame);
static void ota_handle_data(const can_frame_t *frame);
static void ota_check_timeout(void);
static void ota_verify_and_finish(void);

static rt_mq_t ota_queue;
static const struct fal_partition *ota_download_partition;

/* 传输状态的唯一写者是 OTA 工作线程。 */
static cooling_ota_rx_state_t ota_state = COOLING_OTA_RX_IDLE;
static uint32_t ota_rbl_size;
static uint16_t ota_request_crc16;
static uint32_t ota_received_bytes;
static uint16_t ota_expected_seq;
static uint16_t ota_crc16_running;
static rt_tick_t ota_last_activity;

static uint16_t ota_get_u16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static uint32_t ota_get_u32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) |
           data[3];
}

/* tick 差值换算毫秒。 */
static uint32_t ota_ticks_to_ms(rt_tick_t ticks)
{
    return (uint32_t)((ticks * 1000U) / (rt_tick_t)RT_TICK_PER_SECOND);
}

/*
 * CRC16-CCITT-FALSE 逐帧增量更新。算法与 common/fota_crc.c 的
 * fota_crc16_ccitt 完全一致（poly 0x1021、init 0xFFFF、不反射、
 * 输出不异或），这里提供增量形式：数据随帧到达即累计，收满后
 * 无需整区回读即可与请求 CRC16 核对。
 */
static uint16_t ota_crc16_update(uint16_t crc, const uint8_t *data,
                                 size_t length)
{
    size_t index;
    unsigned int bit;

    for (index = 0U; index < length; ++index)
    {
        crc ^= (uint16_t)((uint16_t)data[index] << 8U);
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc & UINT16_C(0x8000)) != 0U
                  ? (uint16_t)((crc << 1U) ^ UINT16_C(0x1021))
                  : (uint16_t)(crc << 1U);
        }
    }

    return crc;
}

static int ota_send_frame(uint8_t frame_class, uint16_t value,
                          const uint8_t *data, uint8_t dlc)
{
    can_frame_t frame;

    frame.id = COOLING_CAN_ID(COOLING_CAN_LOCAL_NODE_ID,
                              COOLING_CAN_MASTER_NODE_ID,
                              frame_class, value);
    frame.format = CAN_FRAME_FORMAT_EXTENDED;
    frame.type = CAN_FRAME_TYPE_DATA;
    frame.dlc = dlc;
    rt_memset(frame.data, 0, sizeof(frame.data));
    rt_memcpy(frame.data, data, dlc);

    return can_bus_send(&frame);
}

/* 准备应答 0x2210：DLC2，data0 = 1 接受且擦除完成 / 0 拒绝，data1 保留 0。 */
static void ota_send_prepare_reply(rt_uint8_t result)
{
    uint8_t data[COOLING_CAN_DLC_OTA_ACK] = {0};

    data[0] = result;
    (void)ota_send_frame(COOLING_CAN_CLASS_NORMAL,
                         COOLING_CAN_FUNC_OTA_REQUEST, data, sizeof(data));
}

/* 逐帧 ACK：类别 1、低 16 位同序号，DLC2，data0 = 1 成功 / 0 失败。 */
static void ota_send_data_ack(uint16_t seq, rt_uint8_t result)
{
    uint8_t data[COOLING_CAN_DLC_OTA_ACK] = {0};

    data[0] = result;
    (void)ota_send_frame(COOLING_CAN_CLASS_OTA_DATA, seq,
                         data, sizeof(data));
}

/* 完成帧 0x2220：DLC4，data0 结果，data1 失败原因，data2..3 保留 0。 */
static void ota_send_complete(rt_uint8_t result, rt_uint8_t reason)
{
    uint8_t data[COOLING_CAN_DLC_OTA_COMPLETE] = {0};

    data[0] = result;
    data[1] = reason;
    (void)ota_send_frame(COOLING_CAN_CLASS_NORMAL,
                         COOLING_CAN_FUNC_OTA_COMPLETE, data, sizeof(data));
}

void cooling_ota_receive(const can_frame_t *frame)
{
    cooling_ota_msg_t msg;
    uint16_t seq = COOLING_CAN_ID_VALUE(frame->id);

    if (COOLING_CAN_ID_CLASS(frame->id) == COOLING_CAN_CLASS_NORMAL)
    {
        if (frame->dlc != COOLING_CAN_DLC_OTA_REQUEST)
        {
            ota_send_prepare_reply(COOLING_CAN_OTA_RESULT_FAIL);
            return;
        }
        msg.type = COOLING_OTA_MSG_REQUEST;
    }
    else
    {
        msg.type = COOLING_OTA_MSG_DATA;
    }
    msg.frame = *frame;

    if ((ota_queue == RT_NULL) ||
        (rt_mq_send(ota_queue, &msg, sizeof(msg)) != RT_EOK))
    {
        if (msg.type == COOLING_OTA_MSG_REQUEST)
        {
            ota_send_prepare_reply(COOLING_CAN_OTA_RESULT_FAIL);
        }
        else
        {
            ota_send_data_ack(seq, COOLING_CAN_OTA_RESULT_FAIL);
        }
    }
}

/*
 * 请求帧处理（工作线程）：校验 -> 置忙 + 安全 OTA 联锁 + 关加热 ->
 * 写 DOWNLOADING 元数据 + 擦除下载区 -> 准备应答。
 */
static void ota_handle_request(const can_frame_t *frame)
{
    uint32_t size;
    uint16_t crc16;

    crc16 = ota_get_u16(&frame->data[2]);
    size = ota_get_u32(&frame->data[4]);

    /*
     * 合法性检查：至少一个 RBL 头且不超过下载分区容量。
     * 非法请求不触碰 Flash 与元数据，
     * 只回准备失败。
     */
    if ((size < FOTA_RBL_HEADER_SIZE) ||
        (size > ota_download_partition->len))
    {
        ota_send_prepare_reply(COOLING_CAN_OTA_RESULT_FAIL);
        return;
    }

    /* 已在接收/校验中：拒绝新请求。 */
    if (ota_state != COOLING_OTA_RX_IDLE)
    {
        ota_send_prepare_reply(COOLING_CAN_OTA_RESULT_FAIL);
        return;
    }

    ota_state = COOLING_OTA_RX_PREPARING;
    ota_rbl_size = size;
    ota_request_crc16 = crc16;
    ota_received_bytes = 0U;
    ota_expected_seq = 0U;
    ota_crc16_running = UINT16_C(0xFFFF);
    ota_last_activity = rt_tick_get();

    /*
     * 进入合法升级准备：先置安全 OTA 联锁，再关闭加热，最后擦写。
     * 加热模块的 false 请求永远允许并立即写低，无需回退直接拉引脚。
     */
    cooling_safety_set_ota(RT_TRUE);
    (void)cooling_heater_set(RT_FALSE);

    /* 写 DOWNLOADING 元数据并整区擦除，全部成功才回复准备=1。 */
    if ((fota_meta_set(FOTA_META_DOWNLOADING,
                       ota_rbl_size, 0U, 0U, 0U) != 0) ||
        (fal_partition_erase_all(ota_download_partition) !=
         (int)ota_download_partition->len))
    {
        (void)fota_meta_set(FOTA_META_INVALID,
                            ota_rbl_size, 0U, 0U, 0U);
        ota_state = COOLING_OTA_RX_IDLE;
        cooling_safety_set_ota(RT_FALSE);
        ota_send_prepare_reply(COOLING_CAN_OTA_RESULT_FAIL);
        return;
    }

    ota_state = COOLING_OTA_RX_RECEIVING;
    ota_last_activity = rt_tick_get();
    ota_send_prepare_reply(COOLING_CAN_OTA_RESULT_OK);
}

/*
 * 数据帧处理（工作线程）：按序写 Flash 并逐帧 ACK；
 * 最后一帧 ACK 后进入两级校验。
 */
static void ota_handle_data(const can_frame_t *frame)
{
    uint16_t seq;
    uint32_t offset;
    uint32_t remaining;
    uint8_t expected_dlc;
    rt_uint8_t ack_result;
    rt_bool_t finished = RT_FALSE;

    seq = COOLING_CAN_ID_VALUE(frame->id);
    offset = (uint32_t)seq * COOLING_CAN_OTA_FRAME_DATA_SIZE;

    if ((ota_state == COOLING_OTA_RX_RECEIVING) &&
        (seq == ota_expected_seq))
    {
        /* 期望序号：DLC 必须等于剩余字节数（封顶 8）。 */
        remaining = ota_rbl_size - ota_received_bytes;
        expected_dlc = remaining < COOLING_CAN_OTA_FRAME_DATA_SIZE
                       ? (uint8_t)remaining : COOLING_CAN_OTA_FRAME_DATA_SIZE;
        if (frame->dlc != expected_dlc)
        {
            ack_result = COOLING_CAN_OTA_RESULT_FAIL;
        }
        else if (fal_partition_write(ota_download_partition, offset,
                                     frame->data, frame->dlc) !=
                 (int)frame->dlc)
        {
            ack_result = COOLING_CAN_OTA_RESULT_FAIL;
        }
        else
        {
            /* Flash 写成功才推进序号、增量 CRC 并刷新活动时间。 */
            ota_received_bytes += frame->dlc;
            ota_expected_seq = (uint16_t)(ota_expected_seq + 1U);
            ota_crc16_running = ota_crc16_update(ota_crc16_running,
                                                 frame->data, frame->dlc);
            ota_last_activity = rt_tick_get();
            ack_result = COOLING_CAN_OTA_RESULT_OK;
            if (ota_received_bytes == ota_rbl_size)
            {
                /* 收满请求声明的字节数即自动完成，不需要额外 END 帧。 */
                ota_state = COOLING_OTA_RX_VERIFYING;
                finished = RT_TRUE;
            }
        }
    }
    else if ((ota_state == COOLING_OTA_RX_RECEIVING) &&
             (seq < ota_expected_seq))
    {
        uint32_t duplicate_remaining = ota_rbl_size - offset;
        uint8_t duplicate_dlc = duplicate_remaining <
                                COOLING_CAN_OTA_FRAME_DATA_SIZE
                                ? (uint8_t)duplicate_remaining
                                : COOLING_CAN_OTA_FRAME_DATA_SIZE;

        /*
         * 接收期内、长度正确的重复旧序号视为 ACK 丢失，回复成功但
         * 绝不重复写 Flash。超时退出后的旧数据帧不能继续收到成功 ACK。
         */
        if (frame->dlc == duplicate_dlc)
        {
            ota_last_activity = rt_tick_get();
            ack_result = COOLING_CAN_OTA_RESULT_OK;
        }
        else
        {
            ack_result = COOLING_CAN_OTA_RESULT_FAIL;
        }
    }
    else
    {
        /* 未来序号/错序号/准备期数据帧：ACK 失败。 */
        ack_result = COOLING_CAN_OTA_RESULT_FAIL;
    }

    ota_send_data_ack(seq, ack_result);

    if (finished)
    {
        ota_verify_and_finish();
    }
}

/*
 * 帧间超时检查（工作线程，带超时接收队列实现）：
 * 只终止 RECEIVING 状态，PREPARING/VERIFYING 不受影响。
 */
static void ota_check_timeout(void)
{
    if (ota_state != COOLING_OTA_RX_RECEIVING)
    {
        return;
    }

    if (ota_ticks_to_ms(rt_tick_get() - ota_last_activity) <
        COOLING_OTA_FRAME_TIMEOUT_MS)
    {
        return;
    }

    (void)fota_meta_set(FOTA_META_INVALID, ota_rbl_size, 0U, 0U, 0U);
    ota_state = COOLING_OTA_RX_IDLE;
    cooling_safety_set_ota(RT_FALSE);
    ota_send_complete(COOLING_CAN_OTA_RESULT_FAIL,
                      COOLING_CAN_OTA_FAIL_TIMEOUT);
}

/*
 * 两级校验 + 元数据 + 完成帧（工作线程）。校验期间状态已是
 * VERIFYING，超时机制不再适用；队列中的后续帧会被依次拒绝。
 */
static void ota_verify_and_finish(void)
{
    fota_storage_reader_t reader;
    fota_rbl_header_t header;
    uint32_t meta_crc32;
    rt_uint8_t result;
    rt_uint8_t reason;

    /* 第一级：整包 CRC16-CCITT-FALSE 与请求一致。 */
    if (ota_crc16_running != ota_request_crc16)
    {
        result = COOLING_CAN_OTA_RESULT_FAIL;
        reason = COOLING_CAN_OTA_FAIL_CRC16;
    }
    else
    {
        reader.partition = ota_download_partition;
        if ((fota_rbl_verify(fota_storage_read, &reader, ota_rbl_size,
                             COOLING_APP_SIZE, &header) != FOTA_RBL_OK) ||
            (fota_storage_crc32(ota_download_partition, ota_rbl_size,
                                &meta_crc32) != 0) ||
            (fota_meta_set(FOTA_META_READY, ota_rbl_size, meta_crc32,
                           header.raw_size, header.code_crc) != 0))
        {
            result = COOLING_CAN_OTA_RESULT_FAIL;
            reason = COOLING_CAN_OTA_FAIL_OTHER;
        }
        else
        {
            result = COOLING_CAN_OTA_RESULT_OK;
            reason = 0U;
        }
    }

    if (result != COOLING_CAN_OTA_RESULT_OK)
    {
        (void)fota_meta_set(FOTA_META_INVALID, ota_rbl_size, 0U, 0U, 0U);
        ota_state = COOLING_OTA_RX_IDLE;
        cooling_safety_set_ota(RT_FALSE);
    }

    ota_send_complete(result, reason);

    if (result == COOLING_CAN_OTA_RESULT_OK)
    {
        /* 短延时保证完成帧进入发送队列后再复位。 */
        rt_thread_mdelay(COOLING_OTA_RESET_DELAY_MS);
        NVIC_SystemReset();
    }
}

/*
 * OTA 工作线程：消息队列的唯一消费者，也是传输状态、expected_seq、
 * received_bytes、CRC16 与 last_activity 的唯一写者。
 * 带超时接收实现 1 秒帧间超时，替代旧的独立监控线程。
 */
static void ota_worker_entry(void *parameter)
{
    cooling_ota_msg_t msg;
    rt_tick_t timeout =
        rt_tick_from_millisecond(COOLING_OTA_TIMEOUT_CHECK_MS);

    (void)parameter;

    while (1)
    {
        rt_err_t result = rt_mq_recv(ota_queue, &msg, sizeof(msg), timeout);

        if (result == -RT_ETIMEOUT)
        {
            /* 空闲检查：只终止 RECEIVING 状态。 */
            ota_check_timeout();
            continue;
        }
        if (result != RT_EOK)
        {
            continue;
        }

        if (msg.type == COOLING_OTA_MSG_REQUEST)
        {
            ota_handle_request(&msg.frame);
        }
        else
        {
            ota_handle_data(&msg.frame);
        }

        /*
         * 即使错误/错序数据持续占满队列，也要检查帧间超时；否则只有
         * 队列空闲时才检查，会被无效帧流无限拖住 RECEIVING 状态。
         */
        ota_check_timeout();
    }
}

rt_err_t cooling_ota_start(void)
{
    rt_thread_t thread;
    rt_err_t result;

    if (fal_init() < 0)
    {
        return -RT_ERROR;
    }
    ota_download_partition = fal_partition_find(COOLING_FAL_DOWNLOAD_PART_NAME);
    if (ota_download_partition == RT_NULL)
    {
        return -RT_ERROR;
    }

    ota_queue = rt_mq_create("otaq",
                             sizeof(cooling_ota_msg_t),
                             COOLING_OTA_QUEUE_CAPACITY,
                             RT_IPC_FLAG_PRIO);
    if (ota_queue == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    thread = rt_thread_create("otawork",
                              ota_worker_entry,
                              RT_NULL,
                              COOLING_OTA_THREAD_STACK_SIZE,
                              COOLING_OTA_THREAD_PRIORITY,
                              COOLING_OTA_THREAD_TIMESLICE);
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
