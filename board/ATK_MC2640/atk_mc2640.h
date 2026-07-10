/**
 ****************************************************************************************************
 * @file        atk_mc2640.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2022-06-21
 * @brief       ATK-MC2640模块驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 探索者 F407开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#ifndef __ATK_MC2640_H
#define __ATK_MC2640_H

#include "stm32f4xx_hal.h"

/* 定义使用DCMI接口或直接GPIO驱动 */
#define ATK_MC2640_USING_DCMI                   1

#if (ATK_MC2640_USING_DCMI == 0)
/* 定义是否使用同一GPIO端口连接ATK-MC2640的D0~D7数据引脚 */
#define ATK_MC2640_DATA_PIN_IN_SAME_GPIO_PORT   0
#endif

/* 定义ATK-MC2640模块的闪光灯是否由OV2640控制 */
#define ATK_MC2640_LED_CTL_BY_OV2640            1

#if (ATK_MC2640_USING_DCMI == 0)
#if (ATK_MC2640_DATA_PIN_IN_SAME_GPIO_PORT != 0)
/* 连接ATK-MC2640模块D0~D7的GPIO端口 */
#define ATK_MC2640_DATE_GPIO_PORT  GPIOC
/* 一次性读取连接至ATK-MC2640的D0~D7的GPIO引脚数据的掩码 */
#define ATK_MC2640_DATA_READ_MASK  0x00FF
#endif
#endif

#if (ATK_MC2640_USING_DCMI == 0)
/* 传输图像数据的DMA相关定义 */
#define ATK_MC2640_DMA_INTERFACE                DMA2_Stream0
#define ATK_MC2640_DMA_CHANNEL                  DMA_CHANNEL_0
#define ATK_MC2640_DMA_CLK_ENABLE()             do{ __HAL_RCC_DMA2_CLK_ENABLE(); }while(0)
#endif

/* 引脚定义 */
#if (ATK_MC2640_USING_DCMI == 0)
#define ATK_MC2640_VSYNC_GPIO_PORT              GPIOB
#define ATK_MC2640_VSYNC_GPIO_PIN               GPIO_PIN_7
#define ATK_MC2640_VSYNC_GPIO_CLK_ENABLE()      do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)
#define ATK_MC2640_HREF_GPIO_PORT               GPIOA
#define ATK_MC2640_HREF_GPIO_PIN                GPIO_PIN_4
#define ATK_MC2640_HREF_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)
#define ATK_MC2640_D0_GPIO_PORT                 GPIOC
#define ATK_MC2640_D0_GPIO_PIN                  GPIO_PIN_6
#define ATK_MC2640_D0_GPIO_CLK_ENABLE()         do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)
#define ATK_MC2640_D1_GPIO_PORT                 GPIOC
#define ATK_MC2640_D1_GPIO_PIN                  GPIO_PIN_7
#define ATK_MC2640_D1_GPIO_CLK_ENABLE()         do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)
#define ATK_MC2640_D2_GPIO_PORT                 GPIOC
#define ATK_MC2640_D2_GPIO_PIN                  GPIO_PIN_8
#define ATK_MC2640_D2_GPIO_CLK_ENABLE()         do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)
#define ATK_MC2640_D3_GPIO_PORT                 GPIOC
#define ATK_MC2640_D3_GPIO_PIN                  GPIO_PIN_9
#define ATK_MC2640_D3_GPIO_CLK_ENABLE()         do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)
#define ATK_MC2640_D4_GPIO_PORT                 GPIOC
#define ATK_MC2640_D4_GPIO_PIN                  GPIO_PIN_11
#define ATK_MC2640_D4_GPIO_CLK_ENABLE()         do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)
#define ATK_MC2640_D5_GPIO_PORT                 GPIOB
#define ATK_MC2640_D5_GPIO_PIN                  GPIO_PIN_6
#define ATK_MC2640_D5_GPIO_CLK_ENABLE()         do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)
#define ATK_MC2640_D6_GPIO_PORT                 GPIOE
#define ATK_MC2640_D6_GPIO_PIN                  GPIO_PIN_5
#define ATK_MC2640_D6_GPIO_CLK_ENABLE()         do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)
#define ATK_MC2640_D7_GPIO_PORT                 GPIOE
#define ATK_MC2640_D7_GPIO_PIN                  GPIO_PIN_6
#define ATK_MC2640_D7_GPIO_CLK_ENABLE()         do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)
#define ATK_MC2640_PCLK_GPIO_PORT               GPIOA
#define ATK_MC2640_PCLK_GPIO_PIN                GPIO_PIN_6
#define ATK_MC2640_PCLK_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)
#endif
#define ATK_MC2640_RST_GPIO_PORT                GPIOG
#define ATK_MC2640_RST_GPIO_PIN                 GPIO_PIN_15
#define ATK_MC2640_RST_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOG_CLK_ENABLE(); }while(0)
#define ATK_MC2640_PWDN_GPIO_PORT               GPIOD
#define ATK_MC2640_PWDN_GPIO_PIN                GPIO_PIN_3
#define ATK_MC2640_PWDN_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOD_CLK_ENABLE(); }while(0)
#if (ATK_MC2640_LED_CTL_BY_OV2640 == 0)
#define ATK_MC2640_FLASH_GPIO_PORT              GPIOA
#define ATK_MC2640_FLASH_GPIO_PIN               GPIO_PIN_8
#define ATK_MC2640_FLASH_GPIO_CLK_ENABLE()      do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)
#endif

/* IO操作 */
#define ATK_MC2640_RST(x)                       do{ x ?                                                                                         \
                                                    HAL_GPIO_WritePin(ATK_MC2640_RST_GPIO_PORT, ATK_MC2640_RST_GPIO_PIN, GPIO_PIN_SET) :        \
                                                    HAL_GPIO_WritePin(ATK_MC2640_RST_GPIO_PORT, ATK_MC2640_RST_GPIO_PIN, GPIO_PIN_RESET);       \
                                                }while(0)
#define ATK_MC2640_PWDN(x)                      do{ x ?                                                                                         \
                                                    HAL_GPIO_WritePin(ATK_MC2640_PWDN_GPIO_PORT, ATK_MC2640_PWDN_GPIO_PIN, GPIO_PIN_SET) :      \
                                                    HAL_GPIO_WritePin(ATK_MC2640_PWDN_GPIO_PORT, ATK_MC2640_PWDN_GPIO_PIN, GPIO_PIN_RESET);     \
                                                }while(0)
#if (ATK_MC2640_LED_CTL_BY_OV2640 == 0)
#define ATK_MC2640_FLASH(x)                     do{ x ?                                                                                         \
                                                    HAL_GPIO_WritePin(ATK_MC2640_FLASH_GPIO_PORT, ATK_MC2640_FLASH_GPIO_PIN, GPIO_PIN_SET) :    \
                                                    HAL_GPIO_WritePin(ATK_MC2640_FLASH_GPIO_PORT, ATK_MC2640_FLASH_GPIO_PIN, GPIO_PIN_RESET);   \
                                                }while(0)
#endif
#if (ATK_MC2640_USING_DCMI == 0)
#define ATK_MC2640_READ_VSYNC()                 (ATK_MC2640_VSYNC_GPIO_PORT->IDR & ATK_MC2640_VSYNC_GPIO_PIN)
#define ATK_MC2640_READ_HREF()                  (ATK_MC2640_HREF_GPIO_PORT->IDR & ATK_MC2640_HREF_GPIO_PIN)
#define ATK_MC2640_READ_PCLK()                  (ATK_MC2640_PCLK_GPIO_PORT->IDR & ATK_MC2640_PCLK_GPIO_PIN)
#endif

/* ATK-MC2640 SCCB通讯地址 */
#define ATK_MC2640_SCCB_ADDR                    0x30

/* ATK-MC2640模块灯光模式枚举 */
typedef enum
{
    ATK_MC2640_LIGHT_MODE_AUTO = 0x00,          /* Auto */
    ATK_MC2640_LIGHT_MODE_SUNNY,                /* Sunny */
    ATK_MC2640_LIGHT_MODE_CLOUDY,               /* Cloudy */
    ATK_MC2640_LIGHT_MODE_OFFICE,               /* Office */
    ATK_MC2640_LIGHT_MODE_HOME,                 /* Home */
} atk_mc2640_light_mode_t;

/* ATK-MC2640模块色彩饱和度枚举 */
typedef enum
{
    ATK_MC2640_COLOR_SATURATION_0 = 0x00,       /* +2 */
    ATK_MC2640_COLOR_SATURATION_1,              /* +1 */
    ATK_MC2640_COLOR_SATURATION_2,              /* 0 */
    ATK_MC2640_COLOR_SATURATION_3,              /* -1 */
    ATK_MC2640_COLOR_SATURATION_4,              /* -2 */
} atk_mc2640_color_saturation_t;

/* ATK-MC2640模块亮度枚举 */
typedef enum
{
    ATK_MC2640_BRIGHTNESS_0 = 0x00,             /* +2 */
    ATK_MC2640_BRIGHTNESS_1,                    /* +1 */
    ATK_MC2640_BRIGHTNESS_2,                    /* 0 */
    ATK_MC2640_BRIGHTNESS_3,                    /* -1 */
    ATK_MC2640_BRIGHTNESS_4,                    /* -2 */
} atk_mc2640_brightness_t;

/* ATK-MC2640模块对比度枚举 */
typedef enum
{
    ATK_MC2640_CONTRAST_0 = 0x00,               /* +2 */
    ATK_MC2640_CONTRAST_1,                      /* +1 */
    ATK_MC2640_CONTRAST_2,                      /* 0 */
    ATK_MC2640_CONTRAST_3,                      /* -1 */
    ATK_MC2640_CONTRAST_4,                      /* -2 */
} atk_mc2640_contrast_t;

/* ATK-MC2640模块特殊效果枚举 */
typedef enum
{
    ATK_MC2640_SPECIAL_EFFECT_ANTIQUE = 0x00,   /* Antique */
    ATK_MC2640_SPECIAL_EFFECT_BLUISH,           /* Bluish */
    ATK_MC2640_SPECIAL_EFFECT_GREENISH,         /* Greenish */
    ATK_MC2640_SPECIAL_EFFECT_REDISH,           /* Redish */
    ATK_MC2640_SPECIAL_EFFECT_BW,               /* B&W */
    ATK_MC2640_SPECIAL_EFFECT_NEGATIVE,         /* Negative */
    ATK_MC2640_SPECIAL_EFFECT_BW_NEGATIVE,      /* B&W Negative */
    ATK_MC2640_SPECIAL_EFFECT_NORMAL,           /* Normal */
} atk_mc2640_special_effect_t;

/* ATK-MC2640输出图像格式枚举 */
typedef enum
{
    ATK_MC2640_OUTPUT_FORMAT_RGB565 = 0x00,     /* RGB565 */
    ATK_MC2640_OUTPUT_FORMAT_JPEG,              /* JPEG */
} atk_mc2640_output_format_t;

/* ATK-MC2640获取帧数据方式枚举 */
typedef enum
{
    ATK_MC2640_GET_TYPE_DTS_8B_NOINC = 0x00,    /* 图像数据以字节方式写入目的地址，目的地址固定不变 */
    ATK_MC2640_GET_TYPE_DTS_8B_INC,             /* 图像数据以字节方式写入目的地址，目的地址自动增加 */
    ATK_MC2640_GET_TYPE_DTS_16B_NOINC,          /* 图像数据以半字方式写入目的地址，目的地址固定不变 */
    ATK_MC2640_GET_TYPE_DTS_16B_INC,            /* 图像数据以半字方式写入目的地址，目的地址自动增加 */
    ATK_MC2640_GET_TYPE_DTS_32B_NOINC,          /* 图像数据以字方式写入目的地址，目的地址固定不变 */
    ATK_MC2640_GET_TYPE_DTS_32B_INC,            /* 图像数据以字方式写入目的地址，目的地址自动增加 */
} atk_mc2640_get_type_t;

/* 错误代码 */
#define ATK_MC2640_EOK      0   /* 没有错误 */
#define ATK_MC2640_ERROR    1   /* 错误 */
#define ATK_MC2640_EINVAL   2   /* 非法参数 */
#define ATK_MC2640_ENOMEM   3   /* 内存不足 */
#define ATK_MC2640_EEMPTY   4   /* 资源为空 */

/* 操作函数 */
uint8_t atk_mc2640_init(void);                                                                              /* 初始化ATK-MC2640模块 */
#if (ATK_MC2640_LED_CTL_BY_OV2640 == 0)
void atk_mc2640_led_on(void);                                                                               /* 开启ATK-MC2640模块闪光灯 */
void atk_mc2640_led_off(void);                                                                              /* 关闭ATK-MC2640模块闪光灯 */
#else
void atk_mc2640_led_enable(void);                                                                           /* 闪烁ATK-MC2640模块控制LED */
#endif
uint8_t atk_mc2640_set_light_mode(atk_mc2640_light_mode_t mode);                                            /* 设置ATK-MC2640模块灯光模式 */
uint8_t atk_mc2640_set_color_saturation(atk_mc2640_color_saturation_t saturation);                          /* 设置ATK-MC2640模块色彩饱和度 */
uint8_t atk_mc2640_set_brightness(atk_mc2640_brightness_t brightness);                                      /* 设置ATK-MC2640模块亮度 */
uint8_t atk_mc2640_set_contrast(atk_mc2640_contrast_t contrast);                                            /* 设置ATK-MC2640模块对比度 */
uint8_t atk_mc2640_set_special_effect(atk_mc2640_special_effect_t effect);                                  /* 设置ATK-MC2640模块特殊效果 */
uint8_t atk_mc2640_set_output_format(atk_mc2640_output_format_t format);                                    /* 设置ATK-MC2640模块输出图像格式 */
uint8_t atk_mc2640_set_output_size(uint16_t width, uint16_t height);                                        /* 设置ATK-MC2640模块输出图像分辨率 */
void atk_mc2640_set_sensor_window(uint16_t start_x, uint16_t start_y, uint16_t width, uint16_t height);     /* 设置ATK-MC2640模块传感器窗口 */
uint8_t atk_mc2640_set_image_window(uint16_t off_x, uint16_t off_y, uint16_t width, uint16_t height);       /* 设置ATK-MC2640模块输出图像窗口 */
void atk_mc2640_set_image_size(uint16_t width, uint16_t height);                                            /* 设置ATK-MC2640模块输出图像大小 */
uint8_t atk_mc2640_set_output_speed(uint8_t clk_dev, uint8_t pclk_dev);                                     /* 设置ATK-MC2640模块输出速率 */
void atk_mc2640_colorbar_enable(void);                                                                      /* 开启ATK-MC2640模块彩条测试 */
void atk_mc2640_colorbar_disable(void);                                                                     /* 关闭ATK-MC2640模块彩条测试 */
uint8_t atk_mc2640_get_frame(uint32_t dts_addr, atk_mc2640_get_type_t type, void (*before_transfer)(void)); /* 获取ATK-MC2640模块输出的一帧图像数据 */

#endif
