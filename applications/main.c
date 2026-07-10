#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "eth_camera.h"
#include "datalink.h"
#include <lwip/sockets.h>
#include "lv_port.h"


int main(void)
{
	static ip_addr_t ipaddr;
    uint8_t ip_addr[4];

	ipaddr.addr = inet_addr(RT_LWIP_IPADDR);

	ip_addr[3] = (uint8_t)(ipaddr.addr >> 24);
	ip_addr[2] = (uint8_t)(ipaddr.addr >> 16);
	ip_addr[1] = (uint8_t)(ipaddr.addr >> 8);
	ip_addr[0] = (uint8_t)(ipaddr.addr);


    /* Bring up LVGL in its own thread - won't block eth_camera. */
    lv_port_init();

    rt_thread_mdelay(500);
    rt_kprintf("Camera IP:%d.%d.%d.%d - Video:%d Config:%d\r\n",
               ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3], ETH_PORT, DATALINK_PORT);

    datalink_init();

	eth_camera_capture();
}
