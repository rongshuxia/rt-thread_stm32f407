#ifndef __APP_UI_H__
#define __APP_UI_H__

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    NET_UI_DISCONNECTED = 0,
    NET_UI_CONNECTING,
    NET_UI_CONNECTED,
} net_ui_status_t;

void create_network_gui(void);
void network_gui_set_led_color(uint32_t color_hex);
void network_gui_set_status(net_ui_status_t status);
void network_gui_set_ip(const char *ip_text);
void network_gui_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_UI_H__ */
