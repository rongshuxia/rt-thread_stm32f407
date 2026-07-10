#ifndef __DATALINK_H__
#define __DATALINK_H__

#include <rtthread.h>
#include <stdint.h>

#define DATALINK_PORT               8002
#define DATALINK_TLV_VALUE_MAX      16
#define DATALINK_RECV_BUF_SIZE      256

/* TLV type */
#define DATALINK_TLV_TYPE_IP        0x01    /* value: 4 bytes IPv4 */
#define DATALINK_TLV_TYPE_JPEG_RES  0x02    /* value: width(u16) + height(u16) */
#define DATALINK_TLV_TYPE_ACK       0x7F    /* value: 1 byte, 0=ok */
#define DATALINK_TLV_TYPE_NACK      0x7E    /* value: 1 byte error code */

/* NACK error code */
#define DATALINK_ERR_INVALID_TLV    0x01
#define DATALINK_ERR_INVALID_VALUE  0x02
#define DATALINK_ERR_APPLY_FAILED   0x03

#pragma pack(1)

/* Generic TLV header: type(1) + length(1) + value(n) */
typedef struct
{
    uint8_t type;
    uint8_t length;
    uint8_t value[DATALINK_TLV_VALUE_MAX];
} datalink_tlv_t;

typedef struct
{
    uint8_t addr[4];
} datalink_tlv_ip_t;

typedef struct
{
    uint16_t width;
    uint16_t height;
} datalink_tlv_jpeg_res_t;

#pragma pack()

void datalink_init(void);

#endif
