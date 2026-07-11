/*
 * TCP throughput test for STM32 Ethernet.
 *
 * Usage (MSH):
 *   netperf s [port] [sec]          board as TCP server (PC sends)
 *   netperf c <host> [port] [sec]   board as TCP client (PC receives)
 *
 * PC companion: tool/netperf_pc.py
 */

#include "net_throughput.h"
#include "rtt_log.h"

#include <lwip/netif.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <stdlib.h>
#include <string.h>

#define NETPERF_BUF_SIZE        4096
#define NETPERF_REPORT_MS       1000

static uint8_t netperf_buf[NETPERF_BUF_SIZE];

static void netperf_fill_buf(void)
{
    rt_size_t i;

    for (i = 0; i < NETPERF_BUF_SIZE; i++)
    {
        netperf_buf[i] = (uint8_t)(i & 0xFF);
    }
}

static void netperf_print_rate(const char *tag, rt_uint32_t total_bytes, rt_uint32_t elapsed_ms)
{
    rt_uint32_t mbps_x100;

    if (elapsed_ms == 0)
    {
        elapsed_ms = 1;
    }

    /* Mbit/s * 100 = bytes * 80 / elapsed_ms / 100 (纯整数，避免 %f) */
    mbps_x100 = (rt_uint32_t)(((rt_uint64_t)total_bytes * 80ULL) / elapsed_ms / 100ULL);

    RTT_LOG_I("%s: %u bytes in %u.%03u s, %u.%02u Mbit/s",
              tag,
              total_bytes,
              elapsed_ms / 1000U,
              elapsed_ms % 1000U,
              mbps_x100 / 100U,
              mbps_x100 % 100U);
}

static int netperf_wait_link(void)
{
    rt_tick_t start = rt_tick_get();

    while (netif_default == RT_NULL)
    {
        if ((rt_tick_get() - start) > rt_tick_from_millisecond(10000))
        {
            RTT_LOG_E("network not ready");
            return -1;
        }
        rt_thread_mdelay(200);
    }

    return 0;
}

int netperf_tcp_server(int port, int duration_sec)
{
    int listen_fd = -1;
    int client_fd = -1;
    struct sockaddr_in addr;
    socklen_t addr_len;
    rt_tick_t start_tick;
    rt_tick_t last_report_tick;
    rt_uint32_t total_bytes = 0;
    rt_uint32_t interval_bytes = 0;
    int recv_len;
    int duration_ms;

    if (port <= 0)
    {
        port = NETPERF_PORT_DEFAULT;
    }
    if (duration_sec <= 0)
    {
        duration_sec = NETPERF_DURATION_SEC;
    }

    if (netperf_wait_link() != 0)
    {
        return -1;
    }

    duration_ms = duration_sec * 1000;
    netperf_fill_buf();

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        RTT_LOG_E("socket failed");
        return -1;
    }

    rt_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = PP_HTONS((uint16_t)port);
    addr.sin_addr.s_addr = PP_HTONL(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        RTT_LOG_E("bind port %d failed", port);
        closesocket(listen_fd);
        return -1;
    }

    if (listen(listen_fd, 1) < 0)
    {
        RTT_LOG_E("listen failed");
        closesocket(listen_fd);
        return -1;
    }

    RTT_LOG_I("TCP server listen 0.0.0.0:%d", port);
    RTT_LOG_I("waiting PC connect %ds, run: python netperf_pc.py client 192.168.188.18",
              NETPERF_ACCEPT_TIMEOUT_SEC);

    {
        rt_tick_t wait_start = rt_tick_get();
        fd_set readfds;
        struct timeval tv;

        while (client_fd < 0)
        {
            FD_ZERO(&readfds);
            FD_SET(listen_fd, &readfds);
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            if (select(listen_fd + 1, &readfds, RT_NULL, RT_NULL, &tv) > 0)
            {
                addr_len = sizeof(addr);
                client_fd = accept(listen_fd, (struct sockaddr *)&addr, &addr_len);
                break;
            }

            if ((rt_tick_get() - wait_start) >= rt_tick_from_millisecond(NETPERF_ACCEPT_TIMEOUT_SEC * 1000))
            {
                RTT_LOG_W("accept timeout, no PC connected");
                closesocket(listen_fd);
                return -1;
            }
        }
    }

    closesocket(listen_fd);
    listen_fd = -1;

    if (client_fd < 0)
    {
        RTT_LOG_E("accept failed");
        return -1;
    }

    RTT_LOG_I("client connected, receive %d s...", duration_sec);

    start_tick = rt_tick_get();
    last_report_tick = start_tick;

    while ((int)((rt_tick_get() - start_tick) * 1000 / RT_TICK_PER_SECOND) < duration_ms)
    {
        recv_len = recv(client_fd, netperf_buf, NETPERF_BUF_SIZE, 0);
        if (recv_len <= 0)
        {
            break;
        }

        total_bytes += (rt_uint32_t)recv_len;
        interval_bytes += (rt_uint32_t)recv_len;

        if ((rt_tick_get() - last_report_tick) >= rt_tick_from_millisecond(NETPERF_REPORT_MS))
        {
            rt_uint32_t elapsed_ms = (rt_uint32_t)((rt_tick_get() - start_tick) * 1000 / RT_TICK_PER_SECOND);
            netperf_print_rate("RX interval", interval_bytes, NETPERF_REPORT_MS);
            RTT_LOG_I("RX total: %u bytes, elapsed %u.%03u s",
                      total_bytes, elapsed_ms / 1000U, elapsed_ms % 1000U);
            interval_bytes = 0;
            last_report_tick = rt_tick_get();
        }
    }

    {
        rt_uint32_t elapsed_ms = (rt_uint32_t)((rt_tick_get() - start_tick) * 1000 / RT_TICK_PER_SECOND);
        netperf_print_rate("RX final", total_bytes, elapsed_ms);
    }

    closesocket(client_fd);
    return 0;
}

int netperf_tcp_client(const char *host, int port, int duration_sec)
{
    int sock_fd = -1;
    struct sockaddr_in addr;
    struct hostent *he;
    rt_tick_t start_tick;
    rt_tick_t last_report_tick;
    rt_uint32_t total_bytes = 0;
    rt_uint32_t interval_bytes = 0;
    int send_len;
    int duration_ms;

    if ((host == RT_NULL) || (host[0] == '\0'))
    {
        RTT_LOG_E("host required");
        return -1;
    }
    if (port <= 0)
    {
        port = NETPERF_PORT_DEFAULT;
    }
    if (duration_sec <= 0)
    {
        duration_sec = NETPERF_DURATION_SEC;
    }

    if (netperf_wait_link() != 0)
    {
        return -1;
    }

    duration_ms = duration_sec * 1000;
    netperf_fill_buf();

    he = gethostbyname(host);
    if (he == RT_NULL)
    {
        RTT_LOG_E("resolve %s failed", host);
        return -1;
    }

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0)
    {
        RTT_LOG_E("socket failed");
        return -1;
    }

    rt_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = PP_HTONS((uint16_t)port);
    rt_memcpy(&addr.sin_addr, he->h_addr, sizeof(addr.sin_addr));

    RTT_LOG_I("connect %s:%d for %d s", host, port, duration_sec);
    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        RTT_LOG_E("connect failed");
        closesocket(sock_fd);
        return -1;
    }

    RTT_LOG_I("connected, sending...");

    start_tick = rt_tick_get();
    last_report_tick = start_tick;

    while ((int)((rt_tick_get() - start_tick) * 1000 / RT_TICK_PER_SECOND) < duration_ms)
    {
        send_len = send(sock_fd, netperf_buf, NETPERF_BUF_SIZE, 0);
        if (send_len <= 0)
        {
            break;
        }

        total_bytes += (rt_uint32_t)send_len;
        interval_bytes += (rt_uint32_t)send_len;

        if ((rt_tick_get() - last_report_tick) >= rt_tick_from_millisecond(NETPERF_REPORT_MS))
        {
            rt_uint32_t elapsed_ms = (rt_uint32_t)((rt_tick_get() - start_tick) * 1000 / RT_TICK_PER_SECOND);
            netperf_print_rate("TX interval", interval_bytes, NETPERF_REPORT_MS);
            RTT_LOG_I("TX total: %u bytes, elapsed %u.%03u s",
                      total_bytes, elapsed_ms / 1000U, elapsed_ms % 1000U);
            interval_bytes = 0;
            last_report_tick = rt_tick_get();
        }
    }

    {
        rt_uint32_t elapsed_ms = (rt_uint32_t)((rt_tick_get() - start_tick) * 1000 / RT_TICK_PER_SECOND);
        netperf_print_rate("TX final", total_bytes, elapsed_ms);
    }

    closesocket(sock_fd);
    return 0;
}

#ifdef RT_USING_FINSH
#include <finsh.h>

static int netperf(int argc, char **argv)
{
    int port = NETPERF_PORT_DEFAULT;
    int duration = NETPERF_DURATION_SEC;

    if (argc < 2)
    {
        rt_kprintf("Usage:\n");
        rt_kprintf("  netperf s [port] [sec]          TCP server, PC sends data\n");
        rt_kprintf("  netperf c <host> [port] [sec]   TCP client, PC receives data\n");
        rt_kprintf("Default port %d, duration %d s\n", NETPERF_PORT_DEFAULT, NETPERF_DURATION_SEC);
        return 0;
    }

    if ((argv[1][0] == 's') && (argv[1][1] == '\0'))
    {
        if (argc >= 3)
        {
            port = atoi(argv[2]);
        }
        if (argc >= 4)
        {
            duration = atoi(argv[3]);
        }
        return netperf_tcp_server(port, duration);
    }

    if ((argv[1][0] == 'c') && (argv[1][1] == '\0'))
    {
        if (argc < 3)
        {
            rt_kprintf("Please input host IP\n");
            return -1;
        }
        if (argc >= 4)
        {
            port = atoi(argv[3]);
        }
        if (argc >= 5)
        {
            duration = atoi(argv[4]);
        }
        return netperf_tcp_client(argv[2], port, duration);
    }

    rt_kprintf("Unknown mode: %s\n", argv[1]);
    return -1;
}

MSH_CMD_EXPORT(netperf, "TCP throughput test");
#endif /* RT_USING_FINSH */
