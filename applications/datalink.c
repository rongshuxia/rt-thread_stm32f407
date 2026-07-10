#include "datalink.h"
#include "atk_mc2640.h"
#include <lwip/sockets.h>
#include <lwip/netif.h>
#include <lwip/netifapi.h>
#include <string.h>

#define DATALINK_THREAD_STACK       2048
#define DATALINK_THREAD_PRIORITY    15
#define DATALINK_THREAD_TICK        10

static int datalink_send_tlv(int fd, uint8_t type, const void *value, uint8_t length)
{
    datalink_tlv_t tlv;

    if (length > DATALINK_TLV_VALUE_MAX)
    {
        return -1;
    }

    tlv.type = type;
    tlv.length = length;
    if ((value != RT_NULL) && (length > 0))
    {
        memcpy(tlv.value, value, length);
    }

    return send(fd, &tlv, 2 + length, 0);
}

static int datalink_apply_ip(const datalink_tlv_ip_t *ip_cfg)
{
    ip_addr_t ipaddr;

    if (netif_default == RT_NULL)
    {
        return -1;
    }

    IP4_ADDR(&ipaddr, ip_cfg->addr[0], ip_cfg->addr[1], ip_cfg->addr[2], ip_cfg->addr[3]);
    if (netifapi_netif_set_addr(netif_default, &ipaddr,
                                &netif_default->netmask, &netif_default->gw) != ERR_OK)
    {
        return -1;
    }

    rt_kprintf("datalink: IP set to %d.%d.%d.%d\r\n",
               ip_cfg->addr[0], ip_cfg->addr[1], ip_cfg->addr[2], ip_cfg->addr[3]);
    return 0;
}

static int datalink_apply_jpeg_res(const datalink_tlv_jpeg_res_t *jpeg_cfg)
{
    if (atk_mc2640_set_output_size(jpeg_cfg->width, jpeg_cfg->height) != ATK_MC2640_EOK)
    {
        return -1;
    }

    rt_kprintf("datalink: JPEG resolution set to %dx%d\r\n", jpeg_cfg->width, jpeg_cfg->height);
    return 0;
}

static int datalink_handle_tlv(int fd, const datalink_tlv_t *tlv)
{
    uint8_t ack = 0;
    uint8_t nack;

    switch (tlv->type)
    {
    case DATALINK_TLV_TYPE_IP:
        if (tlv->length != sizeof(datalink_tlv_ip_t))
        {
            nack = DATALINK_ERR_INVALID_VALUE;
            datalink_send_tlv(fd, DATALINK_TLV_TYPE_NACK, &nack, 1);
            return -1;
        }
        if (datalink_apply_ip((const datalink_tlv_ip_t *)tlv->value) != 0)
        {
            nack = DATALINK_ERR_APPLY_FAILED;
            datalink_send_tlv(fd, DATALINK_TLV_TYPE_NACK, &nack, 1);
            return -1;
        }
        break;

    case DATALINK_TLV_TYPE_JPEG_RES:
        if (tlv->length != sizeof(datalink_tlv_jpeg_res_t))
        {
            nack = DATALINK_ERR_INVALID_VALUE;
            datalink_send_tlv(fd, DATALINK_TLV_TYPE_NACK, &nack, 1);
            return -1;
        }
        if (datalink_apply_jpeg_res((const datalink_tlv_jpeg_res_t *)tlv->value) != 0)
        {
            nack = DATALINK_ERR_APPLY_FAILED;
            datalink_send_tlv(fd, DATALINK_TLV_TYPE_NACK, &nack, 1);
            return -1;
        }
        break;

    default:
        nack = DATALINK_ERR_INVALID_TLV;
        datalink_send_tlv(fd, DATALINK_TLV_TYPE_NACK, &nack, 1);
        return -1;
    }

    datalink_send_tlv(fd, DATALINK_TLV_TYPE_ACK, &ack, 1);
    return 0;
}

static void datalink_parse_buffer(int fd, uint8_t *buf, int *buf_len)
{
    int offset = 0;

    while (*buf_len - offset >= 2)
    {
        datalink_tlv_t *tlv = (datalink_tlv_t *)(buf + offset);
        int frame_len;

        if (tlv->length > DATALINK_TLV_VALUE_MAX)
        {
            rt_kprintf("datalink: invalid TLV length %d\r\n", tlv->length);
            *buf_len = 0;
            return;
        }

        frame_len = 2 + tlv->length;
        if (*buf_len - offset < frame_len)
        {
            break;
        }

        datalink_handle_tlv(fd, tlv);
        offset += frame_len;
    }

    if (offset > 0)
    {
        memmove(buf, buf + offset, *buf_len - offset);
        *buf_len -= offset;
    }
}

static void datalink_client_loop(int client_fd)
{
    uint8_t recv_buf[DATALINK_RECV_BUF_SIZE];
    int pending_len = 0;
    int recv_len;
    struct timeval timeout;

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    while (1)
    {
        recv_len = recv(client_fd, recv_buf + pending_len,
                        sizeof(recv_buf) - pending_len, 0);
        if (recv_len <= 0)
        {
            break;
        }

        pending_len += recv_len;
        datalink_parse_buffer(client_fd, recv_buf, &pending_len);
    }
}

static void datalink_thread_entry(void *parameter)
{
    int listen_fd;
    int client_fd;
    int opt = 1;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_len;

    (void)parameter;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        rt_kprintf("datalink: socket create failed\r\n");
        return;
    }

    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DATALINK_PORT);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        rt_kprintf("datalink: bind port %d failed\r\n", DATALINK_PORT);
        closesocket(listen_fd);
        return;
    }

    if (listen(listen_fd, 1) < 0)
    {
        rt_kprintf("datalink: listen failed\r\n");
        closesocket(listen_fd);
        return;
    }

    rt_kprintf("datalink: TCP server listening on port %d\r\n", DATALINK_PORT);

    while (1)
    {
        client_len = sizeof(client_addr);
        client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0)
        {
            rt_thread_mdelay(100);
            continue;
        }

        rt_kprintf("datalink: client connected\r\n");
        datalink_client_loop(client_fd);
        closesocket(client_fd);
        rt_kprintf("datalink: client disconnected\r\n");
    }
}

void datalink_init(void)
{
    rt_thread_t tid;

    tid = rt_thread_create("datalink",
                           datalink_thread_entry,
                           RT_NULL,
                           DATALINK_THREAD_STACK,
                           DATALINK_THREAD_PRIORITY,
                           DATALINK_THREAD_TICK);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }
    else
    {
        rt_kprintf("datalink: thread create failed\r\n");
    }
}
