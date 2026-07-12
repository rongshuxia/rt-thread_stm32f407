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

#define CAM_SRV_STACK_SIZE   3072

struct netconn *g_newconn = 0;
struct rt_semaphore dcmi_sem;

static rt_uint8_t cam_srv_stack[CAM_SRV_STACK_SIZE];
static struct rt_thread cam_srv_thread;

static uint32_t jpeg_buf[JPEG_BUF_SIZE / sizeof(uint32_t)];
static rt_bool_t camera_inited = RT_FALSE;

static rt_thread_t s_lvgl_tid;

static void eth_camera_pause_lvgl(void)
{
    if (s_lvgl_tid == RT_NULL)
    {
        s_lvgl_tid = rt_thread_find("lvgl");
    }

    if (s_lvgl_tid != RT_NULL)
    {
        rt_thread_suspend(s_lvgl_tid);
    }
}

static void eth_camera_resume_lvgl(void)
{
    if (s_lvgl_tid != RT_NULL)
    {
        rt_thread_resume(s_lvgl_tid);
    }
}

static uint32_t jpeg_frame_length(const uint8_t *buf, uint32_t max_len)
{
    uint32_t i;

    for (i = 0; i + 1U < max_len; i++)
    {
        if (buf[i] == 0xFF && buf[i + 1U] == 0xD9)
        {
            return i + 2U;
        }
    }

    return 0;
}

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

    if (!camera_inited)
    {
        eth_camera_init();
        camera_inited = RT_TRUE;
        RTT_LOG_I("jpeg buffer at 0x%08X (%u bytes)", (uint32_t)jpeg_buf, JPEG_BUF_SIZE);
        rt_thread_mdelay(500);
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

        rt_thread_mdelay(200);

        while (1)
        {
            uint8_t ret;

            memset((void *)jpeg_buf, 0, JPEG_BUF_SIZE);

            eth_camera_pause_lvgl();
            ret = atk_mc2640_get_frame_sized((uint32_t)jpeg_buf,
                                             ATK_MC2640_GET_TYPE_DTS_32B_INC,
                                             NULL,
                                             JPEG_BUF_SIZE);
            eth_camera_resume_lvgl();

            if (ret != ATK_MC2640_EOK)
            {
                RTT_LOG_W("capture failed, retry");
                rt_thread_mdelay(5);
                continue;
            }

            jpeg_len = jpeg_frame_length((const uint8_t *)jpeg_buf, JPEG_BUF_SIZE);

            if (jpeg_len < 100U ||
                ((const uint8_t *)jpeg_buf)[0] != 0xFF ||
                ((const uint8_t *)jpeg_buf)[1] != 0xD8)
            {
                RTT_LOG_W("jpeg invalid (len=%u, hdr=%02X %02X)", jpeg_len,
                          ((const uint8_t *)jpeg_buf)[0],
                          ((const uint8_t *)jpeg_buf)[1]);
                rt_thread_mdelay(2);
                continue;
            }

            RTT_LOG_I("jpeg frame size %u", jpeg_len);
            err = netconn_write(client, jpeg_buf, jpeg_len, NETCONN_COPY);

            if ((err == ERR_CLSD) || (err == ERR_RST))
            {
                netconn_close(client);
                netconn_delete(client);
                g_newconn = RT_NULL;
                RTT_LOG_I("Host %d.%d.%d.%d disconnected",
                          remot_addr[0], remot_addr[1], remot_addr[2], remot_addr[3]);
                break;
            }

            if (err != ERR_OK)
            {
                RTT_LOG_W("netconn_write err=%d", err);
            }

            rt_thread_mdelay(2);
        }
    }
}

void eth_camera_capture(void)
{
    rt_err_t ret;

    rt_sem_init(&dcmi_sem, "dcmi_sem", 0, RT_IPC_FLAG_PRIO);

    ret = rt_thread_init(&cam_srv_thread,
                         "cam_srv",
                         eth_camera_server_thread,
                         RT_NULL,
                         cam_srv_stack,
                         sizeof(cam_srv_stack),
                         15,
                         10);
    if (ret == RT_EOK)
    {
        rt_thread_startup(&cam_srv_thread);
        RTT_LOG_I("cam_srv started (stack %u, jpeg %u KB)",
                  CAM_SRV_STACK_SIZE, JPEG_BUF_SIZE / 1024U);
    }
    else
    {
        RTT_LOG_E("failed to create cam_srv thread: %d", ret);
    }
}

int eth_camera_is_streaming(void)
{
    return (g_newconn != RT_NULL);
}
