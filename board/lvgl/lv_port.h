/*
 * Glue between LVGL and RT-Thread: tick source, display port, GUI thread.
 */

#ifndef __LV_PORT_H__
#define __LV_PORT_H__

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Used by LV_TICK_CUSTOM in lv_conf.h. */
uint32_t lv_port_tick_get(void);

/* One-shot init: registers the ILI9341-backed display and spawns
 * the lvgl tick + handler thread. Safe to call from main. */
void lv_port_init(void);

/* Hook for user code to build the UI (called once after disp init).
 * Override the weak default to put your own widgets on the screen. */
void lv_user_app(void);

#ifdef __cplusplus
}
#endif

#endif /* __LV_PORT_H__ */
