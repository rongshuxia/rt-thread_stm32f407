#include "stm32f4xx.h"
#include "board.h"
#include "rtthread.h"
#include "atk_mc2640.h"
#include <rthw.h>
#include "eth_camera.h"
#include <lwip/sockets.h>
#include <lwip/api.h>
#include <lwip/netif.h>
#include <string.h>
#include "rtt_log.h"

struct netconn *g_newconn = 0;
struct rt_semaphore dcmi_sem;

static struct netconn *eth_camera_listen_create(void)
{
    err_t err;
    struct netconn *conn;

    conn = netconn_new(NETCONN_TCP);
    if (conn == RT_NULL)
    {
        RTT_LOG_E("netconn_new failed");
        return RT_NULL;
    }

    err = netconn_bind(conn, IP_ADDR_ANY, ETH_PORT);
    if (err != ERR_OK)
    {
        RTT_LOG_E("netconn_bind port %d failed: %d", ETH_PORT, err);
        netconn_delete(conn);
        return RT_NULL;
    }

    err = netconn_listen(conn);
    if (err != ERR_OK)
    {
        RTT_LOG_E("netconn_listen failed: %d", err);
        netconn_delete(conn);
        return RT_NULL;
    }

    conn->recv_timeout = 50;
    RTT_LOG_I("TCP server listening on 0.0.0.0:%d", ETH_PORT);
    return conn;
}

void eth_camera_init(void)
{
    atk_mc2640_init();

    atk_mc2640_set_output_format(ATK_MC2640_OUTPUT_FORMAT_JPEG);
    atk_mc2640_set_output_size(JPEG_WIDTH_320, JPEG_HEIGHT_240);
    atk_mc2640_set_light_mode(ATK_MC2640_LIGHT_MODE_SUNNY);
    atk_mc2640_set_color_saturation(ATK_MC2640_COLOR_SATURATION_1);
    atk_mc2640_set_brightness(ATK_MC2640_BRIGHTNESS_1);
    atk_mc2640_set_contrast(ATK_MC2640_CONTRAST_2);
    atk_mc2640_set_special_effect(ATK_MC2640_SPECIAL_EFFECT_NORMAL);
}

static void eth_camera_server_thread(void *param)
{
    err_t err;
    struct netconn *conn;
    struct netconn *client;
    ip_addr_t ipaddr;
    uint8_t remot_addr[4];
    u16_t port;
    uint32_t *jpeg_buf;
    uint32_t jpeg_len;

    (void)param;

    while (1)
    {
        while (netif_default == RT_NULL)
        {
            rt_thread_mdelay(200);
        }

        conn = eth_camera_listen_create();
        if (conn != RT_NULL)
        {
            break;
        }

        RTT_LOG_W("TCP listen retry in 1s...");
        rt_thread_mdelay(1000);
    }

    while (1)
    {
        client = RT_NULL;
        err = netconn_accept(conn, &client);

        if (err != ERR_OK)
        {
            continue;
        }

        g_newconn = client;

        netconn_getaddr(client, &ipaddr, &port, 0);
        remot_addr[3] = (uint8_t)(ipaddr.addr >> 24);
        remot_addr[2] = (uint8_t)(ipaddr.addr >> 16);
        remot_addr[1] = (uint8_t)(ipaddr.addr >> 8);
        remot_addr[0] = (uint8_t)(ipaddr.addr);
        RTT_LOG_I("Host %d.%d.%d.%d:%d connected",
                  remot_addr[0], remot_addr[1], remot_addr[2], remot_addr[3], port);

        eth_camera_init();

        jpeg_buf = (uint32_t *)rt_malloc(JPEG_BUF_SIZE);
        if (jpeg_buf == RT_NULL)
        {
            RTT_LOG_E("jpeg buffer alloc failed");
            netconn_close(client);
            netconn_delete(client);
            g_newconn = RT_NULL;
            continue;
        }

        rt_thread_mdelay(1000);

        while (1)
        {
            jpeg_len = JPEG_BUF_SIZE / (sizeof(uint32_t));
            memset((void *)jpeg_buf, 0, JPEG_BUF_SIZE);

            atk_mc2640_get_frame((uint32_t)jpeg_buf, ATK_MC2640_GET_TYPE_DTS_32B_INC, NULL);

            while (jpeg_len > 0)
            {
                if (jpeg_buf[jpeg_len - 1] != 0)
                {
                    break;
                }

                jpeg_len--;
            }

            jpeg_len *= sizeof(uint32_t);

			RTT_LOG_I("jpeg frames size %d",jpeg_len);
            err = netconn_write(client, jpeg_buf, jpeg_len, NETCONN_COPY);

            if ((err == ERR_CLSD) || (err == ERR_RST))
            {
                rt_free((void *)jpeg_buf);
                netconn_close(client);
                netconn_delete(client);
                g_newconn = RT_NULL;
                RTT_LOG_I("Host %d.%d.%d.%d disconnected",
                          remot_addr[0], remot_addr[1], remot_addr[2], remot_addr[3]);
                break;
            }

            rt_thread_mdelay(2);
        }
    }
}

void eth_camera_capture(void)
{
    rt_thread_t tid;

    rt_sem_init(&dcmi_sem, "dcmi_sem", 0, RT_IPC_FLAG_PRIO);

    tid = rt_thread_create("cam_srv",
                           eth_camera_server_thread,
                           RT_NULL,
                           4096,
                           15,
                           10);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }
    else
    {
        RTT_LOG_E("failed to create cam_srv thread");
    }
}

int eth_camera_is_streaming(void)
{
    return (g_newconn != RT_NULL);
}
