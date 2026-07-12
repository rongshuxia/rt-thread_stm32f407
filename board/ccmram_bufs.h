#ifndef __CCMRAM_BUFS_H__
#define __CCMRAM_BUFS_H__

#include "lvgl.h"

#define CCM_LV_DRAW_BUF_PIXELS  (240U * 32U)
#define CCM_NETPERF_BUF_SIZE    4096U

typedef struct
{
    lv_color_t lv_draw[CCM_LV_DRAW_BUF_PIXELS];
    uint8_t netperf[CCM_NETPERF_BUF_SIZE];
} ccmram_bufs_t;

extern ccmram_bufs_t ccmram_bufs;

#define ccm_lv_draw_buf   (ccmram_bufs.lv_draw)
#define ccm_netperf_buf   (ccmram_bufs.netperf)

#endif /* __CCMRAM_BUFS_H__ */
