/*
 * LVGL 与 RT-Thread / ILI9341 移植层。
 *
 * 职责:
 *   - 提供 LVGL 系统 tick (lv_port_tick_get, 见 lv_conf.h LV_TICK_CUSTOM)
 *   - 注册 ILI9341 显示驱动及 flush 回调
 *   - 在独立 "lvgl" 线程中运行 lv_timer_handler()
 *
 * 内存布局:
 *   - 绘制缓冲: CCM ccm_lv_draw_buf, 240×32 RGB565 ≈ 15 KB (单缓冲, 省主 SRAM)
 *   - LVGL 堆:   外扩 SRAM 0x68000000, 768 KB (lv_conf.h LV_MEM_ADR/LV_MEM_SIZE)
 *
 * 显示: ILI9341 FSMC 8080, 竖屏 240×320, 16bpp。
 *
 * 调用: main() -> lv_port_init() 创建线程后即返回，不阻塞 eth_camera 等业务线程。
 * UI 代码通过弱符号 lv_user_app() 注入 (applications/app_ui.c 中覆盖)。
 */

#include "lv_port.h"
#include "lvgl.h"
#include "ili9341.h"
#include "ccmram_bufs.h"
#include <rtconfig.h>

#ifdef BSP_USING_IS62WV51216
#include "is62wv51216.h"
#endif

#define LV_THREAD_STACK_SIZE  4096   /* lvgl 线程栈 */
#define LV_THREAD_PRIORITY    11     /* 低于网络/相机等高优先级任务 */
#define LV_THREAD_TICK        5      /* 主循环 sleep 间隔 (ms)，驱动 lv_timer_handler */

#define DISP_HOR    ILI9341_WIDTH    /* 240 */
#define DISP_VER    ILI9341_HEIGHT   /* 320 */
#define DISP_BUF_LINES   32          /* 局部刷新行数，越大占用 CCM 越多 */
#define DISP_BUF_PIXELS  (DISP_HOR * DISP_BUF_LINES)

/*
 * LVGL 自定义 tick 源 (lv_conf.h: LV_TICK_CUSTOM_SYS_TIME_EXPR)。
 * 返回自启动以来的毫秒数；要求 RT_TICK_PER_SECOND == 1000 时精度最佳。
 */
uint32_t lv_port_tick_get(void)
{
    return (uint32_t)(rt_tick_get() * 1000U / RT_TICK_PER_SECOND);
}

/* LVGL 显示绘制缓冲描述符，像素存储在 CCM 的 ccm_lv_draw_buf */
static lv_disp_draw_buf_t draw_buf;

/*
 * LVGL 刷新回调: 将脏区 RGB565 像素块写入 ILI9341 GRAM。
 * flush 完成后必须调用 lv_disp_flush_ready() 通知 LVGL 可复用 draw_buf。
 */
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    ili9341_flush_area((uint16_t)area->x1, (uint16_t)area->y1,
                       (uint16_t)area->x2, (uint16_t)area->y2,
                       (const uint16_t *)color_p);
    lv_disp_flush_ready(drv);
}

/*
 * 注册 LVGL 显示驱动。
 * 单缓冲模式 (第二个 buf 传 RT_NULL)，适合 RAM 紧张场景。
 */
static void disp_init(void)
{
    static lv_disp_drv_t disp_drv;

    lv_disp_draw_buf_init(&draw_buf, ccm_lv_draw_buf, RT_NULL, DISP_BUF_PIXELS);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = DISP_HOR;
    disp_drv.ver_res  = DISP_VER;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
}

/*
 * 默认 UI 入口 (弱符号)。
 * 应用层在 app_ui.c 中提供强符号实现，构建自定义界面。
 */
RT_WEAK void lv_user_app(void)
{
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "LVGL " LVGL_VERSION_INFO "\n"
                             "STM32F407 + ILI9341 + RT-Thread");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

/*
 * lvgl 线程主函数。
 *
 * 初始化顺序:
 *   1. 外扩 SRAM (LVGL 堆依赖, board 阶段可能已 init, 此处再次调用无害)
 *   2. lv_init()        — 堆分配器指向 0x68000000
 *   3. ili9341_init()   — 面板硬件 + FSMC Bank4
 *   4. disp_init()      — 注册显示驱动
 *   5. lv_user_app()    — 用户界面
 *   6. 强制首帧刷新     — 避免启动后黑屏等待第一次 timer
 *
 * 主循环: lv_timer_handler() 处理动画/定时器/刷新，每 5ms 让出 CPU。
 * eth_camera 可在推流时 suspend 本线程以释放 FSMC 带宽。
 */
static void lvgl_thread_entry(void *p)
{
    (void)p;

#ifdef BSP_USING_IS62WV51216
    is62wv51216_init();
#endif

    lv_init();
    ili9341_init();
    disp_init();

    lv_user_app();

    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);

    while (1)
    {
        lv_timer_handler();
        rt_thread_mdelay(LV_THREAD_TICK);
    }
}

/*
 * 创建并启动 lvgl 线程。
 * 可从 main() 早期调用；线程创建后立即返回，GUI 在后台运行。
 */
void lv_port_init(void)
{
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
