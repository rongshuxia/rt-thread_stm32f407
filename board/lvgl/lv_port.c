/*
 * LVGL <-> RT-Thread / ILI9341 porting layer.
 *
 * - Tick is sourced from rt_tick_get() (RT_TICK_PER_SECOND must be 1000).
 * - Display: ILI9341 over FSMC, 240x320 portrait, 16bpp.
 * - Single draw buffer of 1/10 screen lives in static SRAM (~15 KB).
 * - Flush + task handler run inside a dedicated RT-Thread "lvgl" thread.
 */

#include "lv_port.h"
#include "boot_scr.h"
#include "lvgl.h"
#include "ili9341.h"

#define LV_THREAD_STACK_SIZE  4096
#define LV_THREAD_PRIORITY    11
#define LV_THREAD_TICK        5

#define DISP_HOR    ILI9341_WIDTH
#define DISP_VER    ILI9341_HEIGHT
#define DISP_BUF_LINES   32
#define DISP_BUF_PIXELS  (DISP_HOR * DISP_BUF_LINES)

uint32_t lv_port_tick_get(void)
{
    /* rt_tick_get() returns ticks; with RT_TICK_PER_SECOND=1000 that's ms. */
    return (uint32_t)(rt_tick_get() * 1000U / RT_TICK_PER_SECOND);
}

static lv_disp_draw_buf_t draw_buf;
static lv_color_t  buf_1[DISP_BUF_PIXELS];

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    ili9341_flush_area((uint16_t)area->x1, (uint16_t)area->y1,
                       (uint16_t)area->x2, (uint16_t)area->y2,
                       (const uint16_t *)color_p);
    lv_disp_flush_ready(drv);
}

static void disp_init(void)
{
    static lv_disp_drv_t disp_drv;

    lv_disp_draw_buf_init(&draw_buf, buf_1, RT_NULL, DISP_BUF_PIXELS);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = DISP_HOR;
    disp_drv.ver_res  = DISP_VER;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
}

/* Default no-op UI hook. User overrides this in their own code. */
RT_WEAK void lv_user_app(void)
{
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "LVGL " LVGL_VERSION_INFO "\n"
                             "STM32F407 + ILI9341 + RT-Thread");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

static void lvgl_thread_entry(void *p)
{
    (void)p;

    lv_init();
    ili9341_init();
    disp_init();

    lv_user_app();

    /* Force a full-screen first paint to avoid partial-buffer artifacts. */
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);

    while (1)
    {
        lv_timer_handler();
        rt_thread_mdelay(LV_THREAD_TICK);
    }
}

void lv_port_init(void)
{
    boot_scr_preinit();

    rt_thread_t tid = rt_thread_create("lvgl",
                                       lvgl_thread_entry, RT_NULL,
                                       LV_THREAD_STACK_SIZE,
                                       LV_THREAD_PRIORITY, 10);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }
    else
    {
        rt_kprintf("lvgl: failed to create thread\n");
    }
}
