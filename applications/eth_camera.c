#include "stm32f4xx.h"
#include "board.h"
#include "rtthread.h"
#include "atk_mc2640.h"
#include <rthw.h>
#include "eth_camera.h"
#include <lwip/sockets.h>
#include <lwip/api.h>

struct netconn 				*g_newconn = 0;
struct rt_semaphore			dcmi_sem;


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


void eth_camera_capture(void)
{
    err_t err;
    struct netconn *conn;
    static ip_addr_t ipaddr;
    uint8_t remot_addr[4];
    static u16_t port;

    uint32_t *jpeg_buf;
    uint32_t jpeg_len;
    
	rt_sem_init(&dcmi_sem, "dcmi_sem", 0, RT_IPC_FLAG_PRIO);
	
	conn = netconn_new(NETCONN_TCP); 
	
    netconn_bind(conn, IP_ADDR_ANY, ETH_PORT);                
    netconn_listen(conn);                              

    conn->recv_timeout = 50;                            
    
    while (1)                                          
    {
        err = netconn_accept(conn,&g_newconn);          
            
        if (err == ERR_OK)                              
        {
            netconn_getaddr(g_newconn,&ipaddr,&port,0); 
            remot_addr[3] = (uint8_t)(ipaddr.addr >> 24); 
            remot_addr[2] = (uint8_t)(ipaddr.addr >> 16);
            remot_addr[1] = (uint8_t)(ipaddr.addr >> 8);
            remot_addr[0] = (uint8_t)(ipaddr.addr);
            rt_kprintf("Host :%d.%d.%d.%d Connect to the eth_camera server. Port number:%d\r\n",remot_addr[0],remot_addr[1],remot_addr[2],remot_addr[3],port);
            
            eth_camera_init();
           
            jpeg_buf = (uint32_t *)rt_malloc(JPEG_BUF_SIZE);
            
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
                //rt_kprintf("%d\r\n", jpeg_len);
                
                err = netconn_write(g_newconn, jpeg_buf, jpeg_len, NETCONN_COPY);     
                
                if ((err == ERR_CLSD) || (err == ERR_RST))                          
                {
                    rt_free((void *)jpeg_buf);
                    netconn_close(g_newconn);
                    netconn_delete(g_newconn);
                    rt_kprintf("Host :%d.%d.%d.%d Disconnect the video service\r\n",remot_addr[0], remot_addr[1],remot_addr[2],remot_addr[3]);
                    break;
                }
                
                rt_thread_mdelay(2);  
            }
        }

    }
}


