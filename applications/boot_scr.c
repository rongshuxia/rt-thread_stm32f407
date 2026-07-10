#include "boot_scr.h"
#include "lv_port.h"
#include "eth_camera.h"
#include "datalink.h"
#include "lvgl.h"
#include <lwip/netif.h>
#include <string.h>

#define BOOT_LOG_BUF_SIZE       2048
#define BOOT_LOG_DISPLAY_SIZE   512
#define BOOT_REFRESH_MS         200

static char boot_log_buf[BOOT_LOG_BUF_SIZE];
static rt_size_t boot_log_len;
static rt_mutex_t boot_log_mutex;

static lv_obj_t *boot_ip_label;
static lv_obj_t *boot_log_label;
static char boot_log_view[BOOT_LOG_DISPLAY_SIZE];
static char boot_ip_view[160];

static rt_size_t (*boot_console_write_orig)(rt_device_t dev, rt_off_t pos,
                                            const void *buffer, rt_size_t size);

static void boot_scr_log_append(const void *data, rt_size_t len)
{
    const char *src = (const char *)data;
    rt_size_t i;

    if ((data == RT_NULL) || (len == 0) || (boot_log_mutex == RT_NULL))
    {
        return;
    }

    rt_mutex_take(boot_log_mutex, RT_WAITING_FOREVER);

    for (i = 0; i < len; i++)
    {
        char c = src[i];

        if (c == '\0')
        {
            continue;
        }

        if (boot_log_len >= (BOOT_LOG_BUF_SIZE - 1))
        {
            rt_size_t drop = BOOT_LOG_BUF_SIZE / 4;

            memmove(boot_log_buf, boot_log_buf + drop, boot_log_len - drop);
            boot_log_len -= drop;
            boot_log_buf[boot_log_len] = '\0';
        }

        if (c == '\r')
        {
            continue;
        }

        boot_log_buf[boot_log_len++] = c;
    }

    boot_log_buf[boot_log_len] = '\0';
    rt_mutex_release(boot_log_mutex);
}

static rt_size_t boot_console_write_tap(rt_device_t dev, rt_off_t pos,
                                      const void *buffer, rt_size_t size)
{
    boot_scr_log_append(buffer, size);

    if (boot_console_write_orig != RT_NULL)
    {
        return boot_console_write_orig(dev, pos, buffer, size);
    }

    return size;
}

static void boot_scr_console_hook(void)
{
    rt_device_t console_dev = rt_console_get_device();

    if ((console_dev == RT_NULL) || (boot_console_write_orig != RT_NULL))
    {
        return;
    }

    boot_console_write_orig = console_dev->write;
    console_dev->write = boot_console_write_tap;
}

static void boot_scr_update_ip_view(void)
{
    const char *ip_str = "0.0.0.0";
    const char *mask_str = "0.0.0.0";
    const char *link_str = "DOWN";
    char new_view[sizeof(boot_ip_view)];

    if (netif_default != RT_NULL)
    {
        ip_str = ipaddr_ntoa(&netif_default->ip_addr);
        mask_str = ipaddr_ntoa(&netif_default->netmask);

        if (netif_is_link_up(netif_default))
        {
            link_str = "UP";
        }
    }

    rt_snprintf(new_view, sizeof(new_view),
                "IP: %s\nMask: %s\nLink: %s\nVideo:%d  Config:%d",
                ip_str, mask_str, link_str, ETH_PORT, DATALINK_PORT);

    if (rt_strcmp(new_view, boot_ip_view) != 0)
    {
        rt_strncpy(boot_ip_view, new_view, sizeof(boot_ip_view) - 1);
        boot_ip_view[sizeof(boot_ip_view) - 1] = '\0';
        lv_label_set_text(boot_ip_label, boot_ip_view);
    }
}

static void boot_scr_update_log_view(void)
{
    rt_size_t copy_len;
    char new_view[BOOT_LOG_DISPLAY_SIZE];

    if (boot_log_mutex == RT_NULL)
    {
        return;
    }

    rt_mutex_take(boot_log_mutex, RT_WAITING_FOREVER);

    if (boot_log_len <= (BOOT_LOG_DISPLAY_SIZE - 1))
    {
        copy_len = boot_log_len;
    }
    else
    {
        copy_len = BOOT_LOG_DISPLAY_SIZE - 1;
        memcpy(new_view, boot_log_buf + boot_log_len - copy_len, copy_len);
        new_view[copy_len] = '\0';
    }

    if (copy_len == boot_log_len)
    {
        memcpy(new_view, boot_log_buf, copy_len);
        new_view[copy_len] = '\0';
    }

    rt_mutex_release(boot_log_mutex);

    if (rt_strcmp(new_view, boot_log_view) != 0)
    {
        rt_strncpy(boot_log_view, new_view, sizeof(boot_log_view) - 1);
        boot_log_view[sizeof(boot_log_view) - 1] = '\0';
        lv_label_set_text(boot_log_label, boot_log_view);
    }
}

static void boot_scr_refresh_cb(lv_timer_t *timer)
{
    (void)timer;

    boot_scr_update_ip_view();
    boot_scr_update_log_view();
}

void boot_scr_preinit(void)
{
    if (boot_log_mutex == RT_NULL)
    {
        boot_log_mutex = rt_mutex_create("bootlog", RT_IPC_FLAG_PRIO);
    }
}

void lv_user_app(void)
{
    lv_obj_t *title;
    lv_obj_t *log_title;
    lv_obj_t *spinner;
    lv_timer_t *refresh_timer;

    boot_scr_preinit();
    boot_scr_console_hook();

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x16213E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);

    title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "ETH Camera");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE94560), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    spinner = lv_spinner_create(lv_scr_act(), 900, 60);
    lv_obj_set_size(spinner, 36, 36);
    lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0xE94560), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x0F3460), LV_PART_MAIN);

    boot_ip_label = lv_label_create(lv_scr_act());
    lv_obj_set_width(boot_ip_label, 228);
    lv_label_set_long_mode(boot_ip_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(boot_ip_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(boot_ip_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(boot_ip_label, LV_ALIGN_TOP_LEFT, 6, 78);
    boot_scr_update_ip_view();

    log_title = lv_label_create(lv_scr_act());
    lv_label_set_text(log_title, "RT-Thread Log");
    lv_obj_set_style_text_color(log_title, lv_color_hex(0xA2D2FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(log_title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(log_title, LV_ALIGN_TOP_LEFT, 6, 150);

    boot_log_label = lv_label_create(lv_scr_act());
    lv_obj_set_size(boot_log_label, 228, 96);
    lv_label_set_long_mode(boot_log_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_bg_color(boot_log_label, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(boot_log_label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(boot_log_label, 4, LV_PART_MAIN);
    lv_obj_set_style_text_color(boot_log_label, lv_color_hex(0xEAEAEA), LV_PART_MAIN);
    lv_obj_set_style_text_font(boot_log_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(boot_log_label, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_label_set_text(boot_log_label, "Booting...\n");

    refresh_timer = lv_timer_create(boot_scr_refresh_cb, BOOT_REFRESH_MS, RT_NULL);
    lv_timer_set_repeat_count(refresh_timer, -1);

    rt_kprintf("boot screen ready\n");
}
