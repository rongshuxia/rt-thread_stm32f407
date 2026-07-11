#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "eth_camera.h"
#include <lwip/sockets.h>
#include "lv_port.h"
#include "rtt_log.h"
#include "net_throughput.h"

/* 1: main 里跑 TCP 吞吐测试(RTT 看结果); 0: 正常启动 Camera */
#define NETPERF_TEST_IN_MAIN    0

int main(void)
{
	static ip_addr_t ipaddr;
    uint8_t ip_addr[4];

    RTT_LOG_I("boot");

	ipaddr.addr = inet_addr(RT_LWIP_IPADDR);

	ip_addr[3] = (uint8_t)(ipaddr.addr >> 24);
	ip_addr[2] = (uint8_t)(ipaddr.addr >> 16);
	ip_addr[1] = (uint8_t)(ipaddr.addr >> 8);
	ip_addr[0] = (uint8_t)(ipaddr.addr);


    /* Bring up LVGL in its own thread - won't block eth_camera. */
    lv_port_init();

    /* Let tcpip + PHY link settle before starting the camera server thread. */
    rt_thread_mdelay(3000);
    RTT_LOG_I("Camera IP:%d.%d.%d.%d  Video:%d",
              ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3], ETH_PORT);

#if NETPERF_TEST_IN_MAIN
    /* 板子做 TCP Server，PC 运行: python tool/netperf_pc.py client 192.168.188.18 */
    RTT_LOG_I("netperf server start, port %d", NETPERF_PORT_DEFAULT);
    netperf_tcp_server(NETPERF_PORT_DEFAULT, NETPERF_DURATION_SEC);
    RTT_LOG_I("netperf server done");
#endif

    eth_camera_capture();

    return 0;
}
