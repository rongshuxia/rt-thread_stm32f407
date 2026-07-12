/*
 * 以太网 JPEG 摄像头推流 — OV2640 (ATK_MC2640) + lwIP TCP Server。
 *
 * 功能:
 *   - 板子作为 TCP Server，监听 ETH_PORT (8001)
 *   - PC 连接后循环采集 JPEG 帧并通过 TCP 发送裸 JPEG 数据
 *   - 无自定义帧头/长度字段，PC 端按 SOI(FF D8) ~ EOI(FF D9) 切帧
 *
 * 硬件/驱动:
 *   - 传感器: OV2640，经 DCMI + SCCB 驱动 (board/ATK_MC2640)
 *   - 输出: 320×240 JPEG，缓冲 jpeg_buf 32 KB (主 SRAM)
 *   - dcmi_sem: DCMI 帧完成同步，由 atk_mc2640_dcmi 释放/等待
 *
 * 与 LVGL 协调:
 *   采帧期间 suspend lvgl 线程，避免 FSMC 总线被 LCD 刷新占用，
 *   减少 DCMI DMA 与显示争抢带宽。
 *
 * 线程:
 *   main() -> eth_camera_capture() 创建静态 "cam_srv" 线程 (优先级 15)。
 *   PC 端预览: tool/recv_camera_view.py
 *
 * 对外 API (eth_camera.h):
 *   eth_camera_capture()      — 启动服务线程
 *   eth_camera_is_streaming() — 是否有客户端正在拉流
 */

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

#define CAM_SRV_STACK_SIZE   3072   /* cam_srv 线程栈 (静态分配) */

/* 当前已连接的 TCP 客户端，供 eth_camera_is_streaming() 查询 */
struct netconn *g_newconn = 0;

/* DCMI 帧采集完成信号量，atk_mc2640_dcmi 在中断/DMA 完成时 release */
struct rt_semaphore dcmi_sem;

static rt_uint8_t cam_srv_stack[CAM_SRV_STACK_SIZE];
static struct rt_thread cam_srv_thread;

/* JPEG 帧缓冲，按 uint32_t 对齐便于 DCMI 32 位递增写入 */
static uint32_t jpeg_buf[JPEG_BUF_SIZE / sizeof(uint32_t)];
static rt_bool_t camera_inited = RT_FALSE;

/* lvgl 线程句柄缓存，首次采帧前通过 rt_thread_find 获取 */
static rt_thread_t s_lvgl_tid;

/* 暂停 LVGL 刷新，释放 FSMC 带宽给 DCMI 采帧 */
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

/* 恢复 LVGL 刷新 */
static void eth_camera_resume_lvgl(void)
{
    if (s_lvgl_tid != RT_NULL)
    {
        rt_thread_resume(s_lvgl_tid);
    }
}

/*
 * 在缓冲区内查找 JPEG EOI 标记 (FF D9)，返回有效帧长度。
 * @return 0 表示未找到完整帧尾
 */
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

/*
 * 创建并绑定 TCP 监听 socket。
 * recv_timeout = 50 ms，使 netconn_accept 可周期性超时返回。
 */
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

/*
 * 配置 OV2640 传感器: JPEG 320×240 及图像参数。
 * 仅首次有客户端连接前调用一次 (camera_inited 保护)。
 */
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

/*
 * cam_srv 线程主函数。
 *
 * 流程:
 *   1. 等待 netif_default 就绪，创建 TCP 监听 (失败则 1s 重试)
 *   2. 首次连接前初始化摄像头
 *   3. accept 循环: 每接受一个客户端，进入推流内循环
 *   4. 推流: 采帧 -> 校验 JPEG -> netconn_write -> 客户端断开则回到 accept
 */
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

    /* 等待 lwIP 网卡就绪 */
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

    /* 延迟到首次服务再 init 摄像头，避免阻塞 boot 阶段 */
    if (!camera_inited)
    {
        eth_camera_init();
        camera_inited = RT_TRUE;
        RTT_LOG_I("jpeg buffer at 0x%08X (%u bytes)", (uint32_t)jpeg_buf, JPEG_BUF_SIZE);
        rt_thread_mdelay(500);   /* 传感器稳定时间 */
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

        rt_thread_mdelay(200);   /* 给客户端一点准备时间 */

        /* ---- 单客户端推流循环 ---- */
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

            /* 校验 SOI(FF D8) 及最小合理长度 */
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

            rt_thread_mdelay(2);   /* 轻微节流，避免占满 CPU/网络 */
        }
    }
}

/*
 * 启动 cam_srv 线程 (静态栈，不依赖 heap)。
 * main() 在 lv_port_init() 之后延迟 3s 调用，等待 PHY/链路就绪。
 */
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

/* 是否有 PC 客户端正在连接并拉流 */
int eth_camera_is_streaming(void)
{
    return (g_newconn != RT_NULL);
}
