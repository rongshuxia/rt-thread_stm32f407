#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "eth_camera.h"
#include <lwip/sockets.h>
#include "lv_port.h"
#include "rtt_log.h"


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

    eth_camera_capture();

    return 0;
}
