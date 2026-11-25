/*
 * CoolingCtrlBoard Bootloader 兼容 CAN-FOTA 传输实现
 *
 * 实现早期标准帧控制/数据协议，支持开始、分块、结束、终止和查询，
 * 将接收的 RBL 写入 download 分区并完成 CRC/RBL 校验。当前 App 使用
 * applications/ota/cooling_ota.c 的扩展帧协议，本文件主要供 Bootloader
 * 恢复窗口使用，二者共用底层 CAN 总线和 FOTA 存储模块。
 */

#include "fota_can.h"

#include <string.h>

#include <fal.h>
#include <rtthread.h>

#include "can/can_bus.h"
#include "cooling_flash_layout.h"
#include "fota_crc.h"
#include "fota_meta.h"
#include "fota_rbl.h"
#include "fota_storage.h"

#define FOTA_CAN_OP_BEGIN          0x01U
#define FOTA_CAN_OP_BLOCK_BEGIN    0x02U
#define FOTA_CAN_OP_END            0x03U
#define FOTA_CAN_OP_ABORT          0x04U
#define FOTA_CAN_OP_QUERY          0x05U

#define FOTA_CAN_STATUS_OK         0x00U
#define FOTA_CAN_STATUS_COMMAND    0x01U
#define FOTA_CAN_STATUS_STATE      0x02U
#define FOTA_CAN_STATUS_LENGTH     0x03U
#define FOTA_CAN_STATUS_INDEX      0x04U
#define FOTA_CAN_STATUS_CRC        0x05U
#define FOTA_CAN_STATUS_FLASH      0x06U
#define FOTA_CAN_STATUS_RBL        0x07U
#define FOTA_CAN_STATUS_SIZE       0x08U

typedef struct
{
    fota_can_config_t config;
    const struct fal_partition *download;
    uint8_t block[FOTA_CAN_BLOCK_SIZE];
    uint32_t file_size;
    uint32_t write_offset;
    uint32_t running_crc;
    uint16_t next_block;
    uint16_t block_length;
    uint16_t block_received;
    uint16_t block_crc;
    volatile uint8_t activity;
    volatile uint8_t receiving; /* CAN 线程写、加热控制线程读的跨线程标志 */
    uint8_t block_open;
} fota_can_context_t;

static fota_can_context_t g_fota_can;

static uint16_t fota_can_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t fota_can_u32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static void fota_can_put_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void fota_can_put_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void fota_can_respond(uint8_t operation, uint8_t status,
                             uint16_t block, uint32_t value)
{
    can_frame_t message;

    memset(&message, 0, sizeof(message));
    message.id = g_fota_can.config.response_id;
    message.format = CAN_FRAME_FORMAT_STANDARD;
    message.type = CAN_FRAME_TYPE_DATA;
    message.dlc = 8U;
    message.data[0] = operation;
    message.data[1] = status;
    fota_can_put_u16(&message.data[2], block);
    fota_can_put_u32(&message.data[4], value);
    (void)can_bus_send(&message);
}

static void fota_can_reset_transfer(void)
{
    g_fota_can.file_size = 0U;
    g_fota_can.write_offset = 0U;
    g_fota_can.running_crc = FOTA_CRC32_INITIAL_VALUE;
    g_fota_can.next_block = 0U;
    g_fota_can.block_length = 0U;
    g_fota_can.block_received = 0U;
    g_fota_can.block_crc = 0U;
    /* ABORT 也走本函数：清零 receiving，允许控制任务恢复加热。 */
    g_fota_can.receiving = 0U;
    g_fota_can.block_open = 0U;
}

static void fota_can_handle_begin(const can_frame_t *message)
{
    uint32_t file_size;

    if (message->dlc < 5U)
    {
        fota_can_respond(FOTA_CAN_OP_BEGIN, FOTA_CAN_STATUS_LENGTH, 0U, 0U);
        return;
    }

    file_size = fota_can_u32(&message->data[1]);
    if ((file_size < FOTA_RBL_HEADER_SIZE) ||
        (file_size > COOLING_DOWNLOAD_SIZE))
    {
        fota_can_respond(FOTA_CAN_OP_BEGIN, FOTA_CAN_STATUS_SIZE, 0U,
                         COOLING_DOWNLOAD_SIZE);
        return;
    }

    fota_can_reset_transfer();
    g_fota_can.receiving = 1U;
    g_fota_can.config.prepare();
    if (fota_meta_set(FOTA_META_DOWNLOADING, file_size, 0U, 0U, 0U) != 0)
    {
        g_fota_can.receiving = 0U; /* 元数据写入失败，恢复非接收状态 */
        fota_can_respond(FOTA_CAN_OP_BEGIN, FOTA_CAN_STATUS_FLASH, 0U, 0U);
        return;
    }

    /* 擦除会持续一段时间；上位机必须等收到本响应后再发第一个块。 */
    if (fal_partition_erase_all(g_fota_can.download) !=
        (int)g_fota_can.download->len)
    {
        g_fota_can.receiving = 0U; /* 擦除失败，恢复非接收状态 */
        (void)fota_meta_set(FOTA_META_INVALID, file_size, 0U, 0U, 0U);
        fota_can_respond(FOTA_CAN_OP_BEGIN, FOTA_CAN_STATUS_FLASH, 0U, 0U);
        return;
    }

    g_fota_can.file_size = file_size;
    fota_can_respond(FOTA_CAN_OP_BEGIN, FOTA_CAN_STATUS_OK, 0U,
                     FOTA_CAN_BLOCK_SIZE);
}

static void fota_can_handle_block_begin(const can_frame_t *message)
{
    uint16_t block_index;
    uint16_t block_length;

    if (message->dlc < 7U)
    {
        fota_can_respond(FOTA_CAN_OP_BLOCK_BEGIN, FOTA_CAN_STATUS_LENGTH,
                         g_fota_can.next_block, g_fota_can.write_offset);
        return;
    }

    block_index = fota_can_u16(&message->data[1]);
    block_length = fota_can_u16(&message->data[3]);

    if (!g_fota_can.receiving)
    {
        fota_can_respond(FOTA_CAN_OP_BLOCK_BEGIN, FOTA_CAN_STATUS_STATE,
                         g_fota_can.next_block, g_fota_can.write_offset);
        return;
    }

    /* ACK 丢失后重复发送已完成的块头，直接返回当前进度，避免重复写 Flash。 */
    if (block_index < g_fota_can.next_block)
    {
        fota_can_respond(FOTA_CAN_OP_BLOCK_BEGIN, FOTA_CAN_STATUS_OK,
                         g_fota_can.next_block, g_fota_can.write_offset);
        return;
    }

    if (block_index != g_fota_can.next_block)
    {
        fota_can_respond(FOTA_CAN_OP_BLOCK_BEGIN, FOTA_CAN_STATUS_INDEX,
                         g_fota_can.next_block, g_fota_can.write_offset);
        return;
    }

    if ((block_length == 0U) || (block_length > FOTA_CAN_BLOCK_SIZE) ||
        (g_fota_can.write_offset + block_length > g_fota_can.file_size))
    {
        fota_can_respond(FOTA_CAN_OP_BLOCK_BEGIN, FOTA_CAN_STATUS_LENGTH,
                         g_fota_can.next_block, g_fota_can.write_offset);
        return;
    }

    g_fota_can.block_length = block_length;
    g_fota_can.block_received = 0U;
    g_fota_can.block_crc = fota_can_u16(&message->data[5]);
    g_fota_can.block_open = 1U;
}

static void fota_can_handle_data(const can_frame_t *message)
{
    uint16_t remaining;
    uint16_t copy_size;
    uint16_t crc;

    if (!g_fota_can.receiving || !g_fota_can.block_open ||
        (message->dlc == 0U))
    {
        return;
    }

    remaining = (uint16_t)(g_fota_can.block_length -
                           g_fota_can.block_received);
    copy_size = message->dlc < remaining ? (uint16_t)message->dlc : remaining;
    memcpy(&g_fota_can.block[g_fota_can.block_received], message->data, copy_size);
    g_fota_can.block_received = (uint16_t)(g_fota_can.block_received + copy_size);

    if (g_fota_can.block_received != g_fota_can.block_length)
    {
        return;
    }

    g_fota_can.block_open = 0U;
    crc = fota_crc16_ccitt(g_fota_can.block, g_fota_can.block_length);
    if (crc != g_fota_can.block_crc)
    {
        /* 块 CRC 错误不清除 receiving，允许上位机重发本块；
         * busy 保持置位，加热在此期间继续关闭。 */
        fota_can_respond(FOTA_CAN_OP_BLOCK_BEGIN, FOTA_CAN_STATUS_CRC,
                         g_fota_can.next_block, g_fota_can.write_offset);
        return;
    }

    if (fal_partition_write(g_fota_can.download,
                            g_fota_can.write_offset,
                            g_fota_can.block,
                            g_fota_can.block_length) !=
        (int)g_fota_can.block_length)
    {
        g_fota_can.receiving = 0U; /* 写入失败，终止接收并恢复非接收状态 */
        (void)fota_meta_set(FOTA_META_INVALID, g_fota_can.file_size,
                            0U, 0U, 0U);
        fota_can_respond(FOTA_CAN_OP_BLOCK_BEGIN, FOTA_CAN_STATUS_FLASH,
                         g_fota_can.next_block, g_fota_can.write_offset);
        return;
    }

    g_fota_can.running_crc = fota_crc32_update(g_fota_can.running_crc,
                                                g_fota_can.block,
                                                g_fota_can.block_length);
    g_fota_can.write_offset += g_fota_can.block_length;
    g_fota_can.next_block++;
    fota_can_respond(FOTA_CAN_OP_BLOCK_BEGIN, FOTA_CAN_STATUS_OK,
                     g_fota_can.next_block, g_fota_can.write_offset);
}

static void fota_can_handle_end(const can_frame_t *message)
{
    fota_storage_reader_t reader;
    fota_rbl_header_t header;
    fota_rbl_result_t rbl_result;
    uint32_t expected_crc;
    uint32_t calculated_crc;
    uint32_t flash_crc;

    if (message->dlc < 5U)
    {
        fota_can_respond(FOTA_CAN_OP_END, FOTA_CAN_STATUS_LENGTH,
                         g_fota_can.next_block, g_fota_can.write_offset);
        return;
    }

    if (!g_fota_can.receiving || g_fota_can.block_open ||
        (g_fota_can.write_offset != g_fota_can.file_size))
    {
        fota_can_respond(FOTA_CAN_OP_END, FOTA_CAN_STATUS_STATE,
                         g_fota_can.next_block, g_fota_can.write_offset);
        return;
    }

    expected_crc = fota_can_u32(&message->data[1]);
    calculated_crc = fota_crc32_finish(g_fota_can.running_crc);
    if ((calculated_crc != expected_crc) ||
        (fota_storage_crc32(g_fota_can.download,
                            g_fota_can.file_size, &flash_crc) != 0) ||
        (flash_crc != expected_crc))
    {
        g_fota_can.receiving = 0U;
        (void)fota_meta_set(FOTA_META_INVALID, g_fota_can.file_size,
                            expected_crc, 0U, 0U);
        fota_can_respond(FOTA_CAN_OP_END, FOTA_CAN_STATUS_CRC,
                         g_fota_can.next_block, calculated_crc);
        return;
    }

    reader.partition = g_fota_can.download;
    rbl_result = fota_rbl_verify(fota_storage_read, &reader,
                                 g_fota_can.file_size,
                                 COOLING_APP_SIZE, &header);
    if (rbl_result != FOTA_RBL_OK)
    {
        g_fota_can.receiving = 0U;
        (void)fota_meta_set(FOTA_META_INVALID, g_fota_can.file_size,
                            expected_crc, 0U, 0U);
        fota_can_respond(FOTA_CAN_OP_END, FOTA_CAN_STATUS_RBL,
                         g_fota_can.next_block, (uint32_t)(-rbl_result));
        return;
    }

    if (fota_meta_set(FOTA_META_READY, g_fota_can.file_size, expected_crc,
                      header.raw_size, header.code_crc) != 0)
    {
        g_fota_can.receiving = 0U;
        fota_can_respond(FOTA_CAN_OP_END, FOTA_CAN_STATUS_FLASH,
                         g_fota_can.next_block, g_fota_can.write_offset);
        return;
    }

    g_fota_can.receiving = 0U;
    fota_can_respond(FOTA_CAN_OP_END, FOTA_CAN_STATUS_OK,
                     g_fota_can.next_block, header.code_crc);
    rt_thread_mdelay(50U);
    g_fota_can.config.completed();
}

static void fota_can_handle_control(const can_frame_t *message)
{
    fota_meta_info_t info;

    if (message->dlc == 0U)
    {
        return;
    }

    switch (message->data[0])
    {
    case FOTA_CAN_OP_BEGIN:
        fota_can_handle_begin(message);
        break;
    case FOTA_CAN_OP_BLOCK_BEGIN:
        fota_can_handle_block_begin(message);
        break;
    case FOTA_CAN_OP_END:
        fota_can_handle_end(message);
        break;
    case FOTA_CAN_OP_ABORT:
        fota_can_reset_transfer();
        (void)fota_meta_set(FOTA_META_EMPTY, 0U, 0U, 0U, 0U);
        fota_can_respond(FOTA_CAN_OP_ABORT, FOTA_CAN_STATUS_OK, 0U, 0U);
        break;
    case FOTA_CAN_OP_QUERY:
        if (fota_meta_load(&info) == 0)
        {
            fota_can_respond(FOTA_CAN_OP_QUERY, FOTA_CAN_STATUS_OK,
                             (uint16_t)info.state, g_fota_can.write_offset);
        }
        else
        {
            fota_can_respond(FOTA_CAN_OP_QUERY, FOTA_CAN_STATUS_FLASH, 0U, 0U);
        }
        break;
    default:
        fota_can_respond(message->data[0], FOTA_CAN_STATUS_COMMAND,
                         g_fota_can.next_block, g_fota_can.write_offset);
        break;
    }
}

static void fota_can_receive(const can_frame_t *message)
{
    if ((message->format != CAN_FRAME_FORMAT_STANDARD) ||
        (message->type != CAN_FRAME_TYPE_DATA))
    {
        return;
    }

    if (message->id == g_fota_can.config.control_id)
    {
        g_fota_can.activity = 1U;
        fota_can_handle_control(message);
    }
    else if (message->id == g_fota_can.config.data_id)
    {
        g_fota_can.activity = 1U;
        fota_can_handle_data(message);
    }
}

int fota_can_start(const fota_can_config_t *config)
{
    memset(&g_fota_can, 0, sizeof(g_fota_can));
    g_fota_can.config = *config;
    if (fal_init() < 0)
    {
        return -1;
    }
    g_fota_can.download = fal_partition_find(COOLING_FAL_DOWNLOAD_PART_NAME);
    if (g_fota_can.download == RT_NULL)
    {
        return -1;
    }

    fota_can_reset_transfer();
    return can_bus_start(fota_can_receive);
}

int fota_can_activity_seen(void)
{
    return g_fota_can.activity != 0U;
}

int fota_can_is_receiving(void)
{
    return g_fota_can.receiving != 0U;
}
