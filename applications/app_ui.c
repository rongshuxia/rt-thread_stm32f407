#include "lv_port.h"
#include "eth_camera.h"
#include "lvgl.h"
#include <lwip/netif.h>

#define UI_REFRESH_MS           500
#define UI_TOAST_DURATION_MS      3000

#define UI_COLOR_BG             0x1A1A2E
#define UI_COLOR_CARD           0x252542
#define UI_COLOR_ACCENT         0x4ECDC4
#define UI_COLOR_TEXT           0xF0F0F5
#define UI_COLOR_TEXT_DIM       0x9090A8
#define UI_COLOR_READY          0x2ECC71
#define UI_COLOR_LIVE           0x4ECDC4
#define UI_COLOR_OFFLINE        0xE74C3C

typedef enum
{
    UI_STATE_OFFLINE = 0,
    UI_STATE_READY,
    UI_STATE_LIVE,
} ui_state_t;

static lv_obj_t *status_led;
static lv_obj_t *status_label;
static lv_obj_t *ip_label;
static lv_obj_t *hint_label;
static lv_obj_t *toast_banner;
static lv_obj_t *toast_label;
static lv_timer_t *toast_hide_timer;

static ui_state_t app_ui_get_state(void)
{
    if ((netif_default == RT_NULL) || !netif_is_link_up(netif_default))
    {
        return UI_STATE_OFFLINE;
    }

    if (eth_camera_is_streaming())
    {
        return UI_STATE_LIVE;
    }

    return UI_STATE_READY;
}

static void app_ui_apply_state(ui_state_t state)
{
    switch (state)
    {
    case UI_STATE_OFFLINE:
        lv_led_set_color(status_led, lv_color_hex(UI_COLOR_OFFLINE));
        lv_label_set_text(status_label, "Offline");
        lv_label_set_text(hint_label, "Check network cable");
        lv_obj_add_flag(ip_label, LV_OBJ_FLAG_HIDDEN);
        break;

    case UI_STATE_READY:
        lv_led_set_color(status_led, lv_color_hex(UI_COLOR_READY));
        lv_label_set_text(status_label, "Ready");
        lv_label_set_text(hint_label, "Waiting for client");
        lv_obj_clear_flag(ip_label, LV_OBJ_FLAG_HIDDEN);
        break;

    case UI_STATE_LIVE:
        lv_led_set_color(status_led, lv_color_hex(UI_COLOR_LIVE));
        lv_label_set_text(status_label, "Live");
        lv_label_set_text(hint_label, "Streaming video");
        lv_obj_clear_flag(ip_label, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

static void app_ui_update_ip(void)
{
    const char *ip_str = "--";

    if (netif_default != RT_NULL)
    {
        ip_str = ipaddr_ntoa(&netif_default->ip_addr);
    }

    lv_label_set_text_fmt(ip_label, "%s", ip_str);
}

static void app_ui_toast_hide_cb(lv_timer_t *timer)
{
    lv_obj_fade_out(toast_banner, 250, 0);
    lv_obj_add_flag(toast_banner, LV_OBJ_FLAG_HIDDEN);
    toast_hide_timer = RT_NULL;
    (void)timer;
}

static void app_ui_show_toast(const char *text, uint32_t bg_color)
{
    if (toast_hide_timer != RT_NULL)
    {
        lv_timer_del(toast_hide_timer);
        toast_hide_timer = RT_NULL;
    }

    lv_label_set_text(toast_label, text);
    lv_obj_set_style_bg_color(toast_banner, lv_color_hex(bg_color), LV_PART_MAIN);
    lv_obj_clear_flag(toast_banner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_banner);
    lv_obj_fade_in(toast_banner, 250, 0);

    toast_hide_timer = lv_timer_create(app_ui_toast_hide_cb, UI_TOAST_DURATION_MS, RT_NULL);
    lv_timer_set_repeat_count(toast_hide_timer, 1);
}

static void app_ui_on_state_changed(ui_state_t old_state, ui_state_t new_state)
{
    char msg[64];
    const char *ip_str = "--";

    if (netif_default != RT_NULL)
    {
        ip_str = ipaddr_ntoa(&netif_default->ip_addr);
    }

    if ((old_state == UI_STATE_OFFLINE) && (new_state == UI_STATE_READY))
    {
        rt_snprintf(msg, sizeof(msg), "Network Connected\n%s", ip_str);
        app_ui_show_toast(msg, UI_COLOR_READY);
    }
    else if ((old_state != UI_STATE_LIVE) && (new_state == UI_STATE_LIVE))
    {
        app_ui_show_toast("Client Connected\nStreaming started", UI_COLOR_LIVE);
    }
    else if ((old_state != UI_STATE_OFFLINE) && (new_state == UI_STATE_OFFLINE))
    {
        app_ui_show_toast("Network Disconnected", UI_COLOR_OFFLINE);
    }
}

static void app_ui_refresh_cb(lv_timer_t *timer)
{
    static ui_state_t last_state = (ui_state_t)-1;

    ui_state_t state = app_ui_get_state();

    if (last_state == (ui_state_t)-1)
    {
        last_state = state;
    }
    else if (state != last_state)
    {
        app_ui_on_state_changed(last_state, state);
        app_ui_apply_state(state);
        last_state = state;
    }

    if (state != UI_STATE_OFFLINE)
    {
        app_ui_update_ip();
    }

    (void)timer;
}

static lv_obj_t *app_ui_create_camera_icon(lv_obj_t *parent)
{
    lv_obj_t *icon = lv_obj_create(parent);

    lv_obj_set_size(icon, 80, 64);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(icon, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(icon, 0, LV_PART_MAIN);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *body = lv_obj_create(icon);
    lv_obj_set_size(body, 60, 44);
    lv_obj_set_style_bg_color(body, lv_color_hex(UI_COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(body, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(body, 8, LV_PART_MAIN);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, 4);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lens = lv_obj_create(icon);
    lv_obj_set_size(lens, 28, 28);
    lv_obj_set_style_bg_color(lens, lv_color_hex(0x162447), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lens, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(lens, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_border_width(lens, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(lens, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_align(lens, LV_ALIGN_CENTER, 0, 4);
    lv_obj_clear_flag(lens, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dot = lv_obj_create(icon);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_bg_color(dot, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -4, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    return icon;
}

static lv_obj_t *app_ui_create_status_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_set_size(card, 216, 108);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    status_led = lv_led_create(card);
    lv_obj_set_size(status_led, 12, 12);
    lv_led_set_brightness(status_led, 220);
    lv_obj_align(status_led, LV_ALIGN_TOP_LEFT, 0, 2);

    status_label = lv_label_create(card);
    lv_obj_set_style_text_color(status_label, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 20, 0);

    ip_label = lv_label_create(card);
    lv_obj_set_style_text_color(ip_label, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_text_font(ip_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(ip_label, LV_ALIGN_TOP_LEFT, 0, 32);

    hint_label = lv_label_create(card);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(UI_COLOR_TEXT_DIM), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(hint_label, LV_ALIGN_TOP_LEFT, 0, 58);

    return card;
}

static void app_ui_create_toast(lv_obj_t *parent)
{
    toast_banner = lv_obj_create(parent);
    lv_obj_set_size(toast_banner, 220, 52);
    lv_obj_align(toast_banner, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_opa(toast_banner, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(toast_banner, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(toast_banner, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(toast_banner, 8, LV_PART_MAIN);
    lv_obj_clear_flag(toast_banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(toast_banner, LV_OBJ_FLAG_HIDDEN);

    toast_label = lv_label_create(toast_banner);
    lv_obj_set_width(toast_label, 200);
    lv_label_set_long_mode(toast_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(toast_label, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(toast_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(toast_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(toast_label);
}

/*
 * LVGL 用户界面入口（覆盖 lv_port.c 中的 weak 默认实现）。
 *
 * 产品待机界面：品牌信息、网络/推流状态、设备 IP。
 */
void lv_user_app(void)
{
    lv_obj_t *icon;
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *card;
    lv_obj_t *footer;
    lv_timer_t *refresh_timer;

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(UI_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);

    icon = app_ui_create_camera_icon(lv_scr_act());
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 28);

    title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "Network Camera");
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

    subtitle = lv_label_create(lv_scr_act());
    lv_label_set_text(subtitle, "Ethernet Video Stream");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(UI_COLOR_TEXT_DIM), LV_PART_MAIN);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 122);

    card = app_ui_create_status_card(lv_scr_act());
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 18);

    app_ui_create_toast(lv_scr_act());

    footer = lv_label_create(lv_scr_act());
    lv_label_set_text_fmt(footer, "320 x 240  ·  Port %d", ETH_PORT);
    lv_obj_set_style_text_color(footer, lv_color_hex(UI_COLOR_TEXT_DIM), LV_PART_MAIN);
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -16);

    app_ui_apply_state(app_ui_get_state());
    app_ui_update_ip();

    refresh_timer = lv_timer_create(app_ui_refresh_cb, UI_REFRESH_MS, RT_NULL);
    lv_timer_set_repeat_count(refresh_timer, -1);
}
