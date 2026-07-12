/*
 * Network status monitor UI — LVGL v8, dark tech theme, flex layout.
 * Screen: 240 x 320 (ILI9341 portrait).
 */

#include "app_ui.h"
#include "lv_port.h"
#include "eth_camera.h"
#include "lvgl.h"
#include <rtthread.h>
#include <lwip/netif.h>
#include <lwip/ip4_addr.h>

/* 界面文案（UTF-8 字节序列，避免源文件编码依赖） */
#define UI_TXT_CONNECTED     "\xE5\xB7\xB2\xE8\xBF\x9E\xE6\x8E\xA5"       /* 已连接 */
#define UI_TXT_CONNECTING    "\xE8\xBF\x9E\xE6\x8E\xA5\xE4\xB8\xAD"       /* 连接中 */
#define UI_TXT_DISCONNECTED  "\xE5\xB7\xB2\xE6\x96\xAD\xE5\xBC\x80"       /* 已断开 */
#define UI_TXT_NO_CONN       "\xE6\x9C\xAA\xE8\xBF\x9E\xE6\x8E\xA5"       /* 未连接 */
#define UI_TXT_CONNECTING_IP "\xE8\xBF\x9E\xE6\x8E\xA5\xE4\xB8\xAD..."    /* 连接中... */

#define UI_REFRESH_MS        500   /* 定时轮询 lwIP netif 状态的间隔 */

/* Palette */
#define UI_CLR_BG_TOP        0x1A1F2C
#define UI_CLR_BG_BOT        0x12161F
#define UI_CLR_CARD          0x252D3A
#define UI_CLR_CARD_BORDER   0x323C4D
#define UI_CLR_TEXT          0xE8EDF4
#define UI_CLR_TEXT_DIM      0x8B95A5
#define UI_CLR_LED_CONNECTED 0x10B981
#define UI_CLR_LED_CONNECTING 0xF59E0B
#define UI_CLR_LED_DISCONNECTED 0xEF4444

/* Widget handles */
static lv_obj_t *card_panel;
static lv_obj_t *status_led;
static lv_obj_t *status_label;
static lv_obj_t *ip_label;

/* Styles */
static lv_style_t style_scr;
static lv_style_t style_card;
static lv_style_t style_title;
static lv_style_t style_ip;
static lv_style_t style_caption;
static lv_style_t style_status;
static lv_style_t style_led;
static lv_style_t style_divider;
static lv_style_t style_footer;
static bool styles_ready;

/* 一次性初始化 LVGL 样式，styles_ready 防止重复注册 */
static void network_gui_init_styles(void)
{
    if (styles_ready)
    {
        return;
    }

    lv_style_init(&style_scr);
    lv_style_set_bg_color(&style_scr, lv_color_hex(UI_CLR_BG_TOP));
    lv_style_set_bg_grad_color(&style_scr, lv_color_hex(UI_CLR_BG_BOT));
    lv_style_set_bg_grad_dir(&style_scr, LV_GRAD_DIR_VER);
    lv_style_set_bg_opa(&style_scr, LV_OPA_COVER);
    lv_style_set_border_width(&style_scr, 0);
    lv_style_set_pad_all(&style_scr, 0);

    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, lv_color_hex(UI_CLR_CARD));
    lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
    lv_style_set_border_color(&style_card, lv_color_hex(UI_CLR_CARD_BORDER));
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_border_opa(&style_card, LV_OPA_60);
    lv_style_set_radius(&style_card, 12);
    lv_style_set_pad_all(&style_card, 20);
    lv_style_set_pad_row(&style_card, 12);
    lv_style_set_shadow_width(&style_card, 20);
    lv_style_set_shadow_ofs_y(&style_card, 8);
    lv_style_set_shadow_opa(&style_card, LV_OPA_30);
    lv_style_set_shadow_color(&style_card, lv_color_hex(0x000000));

    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_hex(UI_CLR_TEXT_DIM));
    lv_style_set_text_font(&style_title, &lv_font_montserrat_12);
    lv_style_set_text_letter_space(&style_title, 1);

    lv_style_init(&style_ip);
    lv_style_set_text_color(&style_ip, lv_color_hex(UI_CLR_TEXT));
    lv_style_set_text_font(&style_ip, &lv_font_montserrat_20);
    lv_style_set_text_align(&style_ip, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&style_caption);
    lv_style_set_text_color(&style_caption, lv_color_hex(UI_CLR_TEXT_DIM));
    lv_style_set_text_font(&style_caption, &lv_font_montserrat_12);
    lv_style_set_text_letter_space(&style_caption, 1);

    lv_style_init(&style_status);
    lv_style_set_text_color(&style_status, lv_color_hex(UI_CLR_TEXT));
    lv_style_set_text_font(&style_status, &lv_font_simsun_16_cjk);

    lv_style_init(&style_led);
    lv_style_set_bg_color(&style_led, lv_color_hex(UI_CLR_LED_DISCONNECTED));
    lv_style_set_bg_opa(&style_led, LV_OPA_COVER);
    lv_style_set_border_width(&style_led, 0);
    lv_style_set_radius(&style_led, LV_RADIUS_CIRCLE);
    lv_style_set_shadow_width(&style_led, 8);
    lv_style_set_shadow_spread(&style_led, 1);
    lv_style_set_shadow_opa(&style_led, LV_OPA_50);

    lv_style_init(&style_divider);
    lv_style_set_bg_color(&style_divider, lv_color_hex(UI_CLR_CARD_BORDER));
    lv_style_set_bg_opa(&style_divider, LV_OPA_40);
    lv_style_set_border_width(&style_divider, 0);
    lv_style_set_radius(&style_divider, 0);

    lv_style_init(&style_footer);
    lv_style_set_text_color(&style_footer, lv_color_hex(UI_CLR_TEXT_DIM));
    lv_style_set_text_font(&style_footer, &lv_font_montserrat_12);

    styles_ready = true;
}

/* 更新状态指示灯颜色及同色阴影 */
void network_gui_set_led_color(uint32_t color_hex)
{
    if (status_led == RT_NULL)
    {
        return;
    }

    lv_obj_set_style_bg_color(status_led, lv_color_hex(color_hex), LV_PART_MAIN);
    lv_obj_set_style_shadow_color(status_led, lv_color_hex(color_hex), LV_PART_MAIN);
}

/* 同步状态文字与 LED 颜色（绿/橙/红） */
void network_gui_set_status(net_ui_status_t status)
{
    const char *text;
    uint32_t led_color;

    if (status_label == RT_NULL)
    {
        return;
    }

    switch (status)
    {
    case NET_UI_CONNECTED:
        text = UI_TXT_CONNECTED;
        led_color = UI_CLR_LED_CONNECTED;
        break;
    case NET_UI_CONNECTING:
        text = UI_TXT_CONNECTING;
        led_color = UI_CLR_LED_CONNECTING;
        break;
    default:
        text = UI_TXT_DISCONNECTED;
        led_color = UI_CLR_LED_DISCONNECTED;
        break;
    }

    lv_label_set_text(status_label, text);
    network_gui_set_led_color(led_color);
}

/* 更新 IP 显示；NULL 时显示“未连接” */
void network_gui_set_ip(const char *ip_text)
{
    if (ip_label == RT_NULL)
    {
        return;
    }

    lv_label_set_text(ip_label, (ip_text != RT_NULL) ? ip_text : UI_TXT_NO_CONN);
}

/* 卡片内水平分隔线 */
static lv_obj_t *network_gui_create_divider(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *line = lv_obj_create(parent);

    lv_obj_remove_style_all(line);
    lv_obj_add_style(line, &style_divider, LV_PART_MAIN);
    lv_obj_set_size(line, w, h);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);

    return line;
}

/*
 * 构建网络状态界面，层级：
 *   scr (渐变背景, flex 居中)
 *     └── card_panel
 *           ├── title / divider
 *           ├── ip_caption + ip_label / divider
 *           ├── status_row (LED + status_label)
 *           └── footer
 */
void create_network_gui(void)
{
    lv_obj_t *scr;
    lv_obj_t *title;
    lv_obj_t *ip_caption;
    lv_obj_t *status_row;
    lv_obj_t *footer;

    network_gui_init_styles();

    /* 全屏根容器：纵向 flex，内容居中 */
    scr = lv_scr_act();
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &style_scr, LV_PART_MAIN);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(scr, 16, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 主卡片：左右各留 16px 边距 */
    card_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(card_panel);
    lv_obj_add_style(card_panel, &style_card, LV_PART_MAIN);
    lv_obj_set_width(card_panel, LV_HOR_RES - 32);
    lv_obj_set_height(card_panel, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(card_panel, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(card_panel);
    lv_obj_add_style(title, &style_title, LV_PART_MAIN);
    lv_label_set_text(title, "Network Status");

    network_gui_create_divider(card_panel, lv_pct(100), 1);

    ip_caption = lv_label_create(card_panel);
    lv_obj_add_style(ip_caption, &style_caption, LV_PART_MAIN);
    lv_label_set_text(ip_caption, "LOCAL IP");

    ip_label = lv_label_create(card_panel);
    lv_obj_add_style(ip_label, &style_ip, LV_PART_MAIN);
    lv_obj_set_width(ip_label, lv_pct(100));
    lv_label_set_long_mode(ip_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(ip_label, UI_TXT_NO_CONN);

    network_gui_create_divider(card_panel, lv_pct(100), 1);

    /* 状态行：圆点 LED + 中文状态文字，横向排列 */
    status_row = lv_obj_create(card_panel);
    lv_obj_remove_style_all(status_row);
    lv_obj_set_size(status_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(status_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(status_row, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(status_row, LV_OBJ_FLAG_SCROLLABLE);

    status_led = lv_obj_create(status_row);
    lv_obj_remove_style_all(status_led);
    lv_obj_add_style(status_led, &style_led, LV_PART_MAIN);
    lv_obj_set_size(status_led, 12, 12);
    lv_obj_clear_flag(status_led, LV_OBJ_FLAG_SCROLLABLE);

    status_label = lv_label_create(status_row);
    lv_obj_add_style(status_label, &style_status, LV_PART_MAIN);
    lv_label_set_text(status_label, UI_TXT_DISCONNECTED);

    /* 底部显示分辨率与 eth_camera 监听端口 */
    footer = lv_label_create(card_panel);
    lv_obj_add_style(footer, &style_footer, LV_PART_MAIN);
    lv_label_set_text_fmt(footer, "320x240 · Port %d", ETH_PORT);

    network_gui_set_status(NET_UI_DISCONNECTED);
    network_gui_set_ip(UI_TXT_NO_CONN);
}

/*
 * 从 lwIP 默认 netif 读取链路/IP 状态并刷新界面：
 *   链路 down          → 已断开 / 未连接
 *   链路 up、无 IP     → 连接中 / 连接中...
 *   链路 up、已获 IP   → 已连接 / 实际 IP
 */
void network_gui_refresh(void)
{
    net_ui_status_t status = NET_UI_DISCONNECTED;
    const char *ip_text = UI_TXT_NO_CONN;

    if ((netif_default != RT_NULL) && netif_is_link_up(netif_default))
    {
        if (ip4_addr_isany(netif_ip4_addr(netif_default)))
        {
            status = NET_UI_CONNECTING;
            ip_text = UI_TXT_CONNECTING_IP;
        }
        else
        {
            status = NET_UI_CONNECTED;
            ip_text = ipaddr_ntoa(&netif_default->ip_addr);
        }
    }

    network_gui_set_status(status);
    network_gui_set_ip(ip_text);
}

/* LVGL 定时器回调，周期性调用 network_gui_refresh */
static void network_gui_refresh_cb(lv_timer_t *timer)
{
    network_gui_refresh();
    (void)timer;
}

/* LVGL 线程入口：建界面后立即刷新一次，再启动定时轮询 */
void lv_user_app(void)
{
    lv_timer_t *refresh_timer;

    create_network_gui();
    network_gui_refresh();

    refresh_timer = lv_timer_create(network_gui_refresh_cb, UI_REFRESH_MS, RT_NULL);
    lv_timer_set_repeat_count(refresh_timer, -1);  /* -1 = 无限重复 */
}
