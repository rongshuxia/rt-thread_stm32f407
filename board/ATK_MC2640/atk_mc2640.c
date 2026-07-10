/**
 ****************************************************************************************************
 * @file        atk_mc2640.c
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

#include "atk_mc2640.h"
#include "atk_mc2640_sccb.h"
#include "atk_mc2640_cfg.h"
#include "stm32f4xx_hal.h"
#include "stm32f4_delay.h"

#if (ATK_MC2640_USING_DCMI == 0)
//#include "./MALLOC/malloc.h"
#endif

#if (ATK_MC2640_USING_DCMI != 0)
#include "atk_mc2640_dcmi.h"
#endif

/* ATK-MC2640模块制造商ID和产品ID */
#define ATK_MC2640_MID  0x7FA2
#define ATK_MC2640_PID  0x2642

/* ATK-MC2640寄存器块枚举 */
typedef enum
{
    ATK_MC2640_REG_BANK_DSP = 0x00, /* DSP寄存器块 */
    ATK_MC2640_REG_BANK_SENSOR,     /* Sensor寄存器块 */
} atk_mc2640_reg_bank_t;

/* ATK-MC2640模块数据结构体 */
static struct
{
    struct {
        uint16_t width;
        uint16_t height;
    } output;
    
#if (ATK_MC2640_USING_DCMI == 0)
    struct {
        uint8_t *line_buf;
        DMA_HandleTypeDef dma_handle;
    } read;
#endif
} g_atk_mc2640_sta = {0};

/**
 * @brief       ATK-MC2640模块写寄存器
 * @param       reg: 寄存器地址
 *              dat: 待写入的值
 * @retval      无
 */
static void atk_mc2640_write_reg(uint8_t reg, uint8_t dat)
{
    atk_mc2640_sccb_3_phase_write(ATK_MC2640_SCCB_ADDR, reg, dat);
}

/**
 * @brief       ATK-MC2640模块读寄存器
 * @param       reg: 寄存器的地址
 * @retval      读取到的寄存器值
 */
static uint8_t atk_mc2640_read_reg(uint8_t reg)
{
    uint8_t dat = 0;
    
    atk_mc2640_sccb_2_phase_write(ATK_MC2640_SCCB_ADDR, reg);
    atk_mc2640_sccb_2_phase_read(ATK_MC2640_SCCB_ADDR, &dat);
    
    return dat;
}

/**
 * @brief       设置ATK-MC2640模块启用的寄存器块
 * @param       set: ATK_MC2640_REG_BANK_DSP   : DSP寄存器块
 *                   ATK_MC2640_REG_BANK_SENSOR: Sensor寄存器块
 * @retval      ATK_MC2640_EOK   : 设置ATK-MC2640模块启用的寄存器块成功
 *              ATK_MC2640_EINVAL: 传入参数错误
 */
static uint8_t atk_mc2640_reg_bank_select(atk_mc2640_reg_bank_t bank)
{
    switch (bank)
    {
        case ATK_MC2640_REG_BANK_DSP:
        {
            atk_mc2640_write_reg(ATK_MC2640_REG_BANK_SEL, 0x00);
            break;
        }
        case ATK_MC2640_REG_BANK_SENSOR:
        {
            atk_mc2640_write_reg(ATK_MC2640_REG_BANK_SEL, 0x01);
            break;
        }
        default:
        {
            return ATK_MC2640_EINVAL;
        }
    }
    
    return ATK_MC2640_EOK;
}

/**
 * @brief       ATK-MC2640模块硬件初始化
 * @param       无
 * @retval      无
 */
static void atk_mc2640_hw_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    
    /* 使能GPIO时钟 */
#if (ATK_MC2640_USING_DCMI == 0)
    ATK_MC2640_VSYNC_GPIO_CLK_ENABLE();
    ATK_MC2640_HREF_GPIO_CLK_ENABLE();
    ATK_MC2640_D0_GPIO_CLK_ENABLE();
    ATK_MC2640_D1_GPIO_CLK_ENABLE();
    ATK_MC2640_D2_GPIO_CLK_ENABLE();
    ATK_MC2640_D3_GPIO_CLK_ENABLE();
    ATK_MC2640_D4_GPIO_CLK_ENABLE();
    ATK_MC2640_D5_GPIO_CLK_ENABLE();
    ATK_MC2640_D6_GPIO_CLK_ENABLE();
    ATK_MC2640_D7_GPIO_CLK_ENABLE();
    ATK_MC2640_PCLK_GPIO_CLK_ENABLE();
#endif
    ATK_MC2640_RST_GPIO_CLK_ENABLE();
    ATK_MC2640_PWDN_GPIO_CLK_ENABLE();
    
#if (ATK_MC2640_USING_DCMI == 0)
    /* 初始化VSYNC引脚 */
    gpio_init_struct.Pin    = ATK_MC2640_VSYNC_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_VSYNC_GPIO_PORT, &gpio_init_struct);
    
    /* 初始化HREF引脚 */
    gpio_init_struct.Pin    = ATK_MC2640_HREF_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_HREF_GPIO_PORT, &gpio_init_struct);
    
    /* 初始化D0引脚 */
    gpio_init_struct.Pin    = ATK_MC2640_D0_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D0_GPIO_PORT, &gpio_init_struct);
    
    /* 初始化D1引脚 */
    gpio_init_struct.Pin    = ATK_MC2640_D1_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D1_GPIO_PORT, &gpio_init_struct);
    
    /* 初始化D2引脚 */
    gpio_init_struct.Pin    = ATK_MC2640_D2_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D2_GPIO_PORT, &gpio_init_struct);
    
    /* 初始化D3引脚 */
    gpio_init_struct.Pin    = ATK_MC2640_D3_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D3_GPIO_PORT, &gpio_init_struct);
    
    /* 初始化D4引脚 */
    gpio_init_struct.Pin    = ATK_MC2640_D4_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D4_GPIO_PORT, &gpio_init_struct);
    
    /* 初始化D5引脚 */
    gpio_init_struct.Pin    = ATK_MC2640_D5_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D5_GPIO_PORT, &gpio_init_struct);
    
    /* 初始化D6引脚 */
    gpio_init_struct.Pin    = ATK_MC2640_D6_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D6_GPIO_PORT, &gpio_init_struct);
    
    /* 初始化D7引脚 */
    gpio_init_struct.Pin    = ATK_MC2640_D7_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D7_GPIO_PORT, &gpio_init_struct);
    
    /* 初始化PCLK引脚 */
    gpio_init_struct.Pin    = ATK_MC2640_PCLK_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_PCLK_GPIO_PORT, &gpio_init_struct);
#endif
    
    /* 初始化RST引脚 */
    gpio_init_struct.Pin    = ATK_MC2640_RST_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_RST_GPIO_PORT, &gpio_init_struct);
    
    /* 初始化PWDN引脚 */
    gpio_init_struct.Pin    = ATK_MC2640_PWDN_GPIO_PIN;//PD3
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_PWDN_GPIO_PORT, &gpio_init_struct);
    
#if (ATK_MC2640_LED_CTL_BY_OV2640 == 0)
    /* 初始化FLASH引脚 */
    gpio_init_struct.Pin    = ATK_MC2640_FLASH_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_FLASH_GPIO_PORT, &gpio_init_struct);
#endif
    
    ATK_MC2640_RST(1);
    ATK_MC2640_PWDN(1);
#if (ATK_MC2640_LED_CTL_BY_OV2640 == 0)
    ATK_MC2640_FLASH(0);
#endif
}

/**
 * @brief       ATK-MC2640模块退出掉电模式
 * @param       无
 * @retval      无
 */
static void atk_mc2640_exit_power_down(void)
{
    ATK_MC2640_PWDN(0);
    stm32f4_delay_ms(10);
}

/**
 * @brief       ATK-MC2640模块硬件复位
 * @param       无
 * @retval      无
 */
static void atk_mc2640_hw_reset(void)
{
    ATK_MC2640_RST(0);
    stm32f4_delay_ms(10);
    ATK_MC2640_RST(1);
    stm32f4_delay_ms(10);
}

/**
 * @brief       ATK-MC2640模块软件复位
 * @param       无
 * @retval      无
 */
static void atk_mc2640_sw_reset(void)
{
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_SENSOR);
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_COM7, 0x80);
    stm32f4_delay_ms(50);
}

/**
 * @brief       获取ATK-MC2640模块制造商ID
 * @param       无
 * @retval      制造商ID
 */
static uint16_t atk_mc2640_get_mid(void)
{
    uint16_t mid;
    
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_SENSOR);
    mid = atk_mc2640_read_reg(ATK_MC2640_REG_SENSOR_MIDH) << 8;
    mid |= atk_mc2640_read_reg(ATK_MC2640_REG_SENSOR_MIDL);
    
    return mid;
}

/**
 * @brief       获取ATK-MC2640模块产品ID
 * @param       无
 * @retval      产品ID
 */
static uint16_t atk_mc2640_get_pid(void)
{
    uint16_t pid;
    
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_SENSOR);
    pid = atk_mc2640_read_reg(ATK_MC2640_REG_SENSOR_PIDH) << 8;
    pid |= atk_mc2640_read_reg(ATK_MC2640_REG_SENSOR_PIDL);
    
    return pid;
}

/**
 * @brief       初始化ATK-MC2640寄存器配置
 * @param       无
 * @retval      无
 */
static void atk_mc2640_init_reg(void)
{
    uint32_t cfg_index;
    uint8_t zmow;
    uint8_t zmoh;
    uint8_t zmhh;
    
    for (cfg_index=0; cfg_index<(sizeof(atk_mc2640_init_uxga_cfg)/sizeof(atk_mc2640_init_uxga_cfg[0])); cfg_index++)
    {
        atk_mc2640_write_reg(atk_mc2640_init_uxga_cfg[cfg_index][0], atk_mc2640_init_uxga_cfg[cfg_index][1]);
    }
    
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
    zmow = atk_mc2640_read_reg(ATK_MC2640_REG_DSP_ZMOW);
    zmoh = atk_mc2640_read_reg(ATK_MC2640_REG_DSP_ZMOH);
    zmhh = atk_mc2640_read_reg(ATK_MC2640_REG_DSP_ZMHH);
    
    g_atk_mc2640_sta.output.width = ((uint16_t)zmow | ((zmhh & 0x03) << 8)) << 2;
    g_atk_mc2640_sta.output.height = ((uint16_t)zmoh | ((zmhh & 0x04) << 6)) << 2;
}

#if (ATK_MC2640_USING_DCMI == 0)
/**
 * @brief       获取ATK-MD2640端口D0~D7的一字节数据
 * @param       无
 * @retval      ATK-MD2640端口D0~D7的一字节数据
 */
static inline uint8_t atk_mc2640_get_byte_data(void)
{
#if (ATK_MC2640_DATA_PIN_IN_SAME_GPIO_PORT == 0)
    uint8_t dat = 0;
    
    dat |= (((ATK_MC2640_D0_GPIO_PORT->IDR & ATK_MC2640_D0_GPIO_PIN) == 0) ? (0) : (1)) << 0;
    dat |= (((ATK_MC2640_D1_GPIO_PORT->IDR & ATK_MC2640_D1_GPIO_PIN) == 0) ? (0) : (1)) << 1;
    dat |= (((ATK_MC2640_D2_GPIO_PORT->IDR & ATK_MC2640_D2_GPIO_PIN) == 0) ? (0) : (1)) << 2;
    dat |= (((ATK_MC2640_D3_GPIO_PORT->IDR & ATK_MC2640_D3_GPIO_PIN) == 0) ? (0) : (1)) << 3;
    dat |= (((ATK_MC2640_D4_GPIO_PORT->IDR & ATK_MC2640_D4_GPIO_PIN) == 0) ? (0) : (1)) << 4;
    dat |= (((ATK_MC2640_D5_GPIO_PORT->IDR & ATK_MC2640_D5_GPIO_PIN) == 0) ? (0) : (1)) << 5;
    dat |= (((ATK_MC2640_D6_GPIO_PORT->IDR & ATK_MC2640_D6_GPIO_PIN) == 0) ? (0) : (1)) << 6;
    dat |= (((ATK_MC2640_D7_GPIO_PORT->IDR & ATK_MC2640_D7_GPIO_PIN) == 0) ? (0) : (1)) << 7;
    
    return dat;
#else
    return ATK_MC2640_DATE_GPIO_PORT->IDR & ATK_MC2640_DATA_READ_MASK;
#endif
}
#endif

#if (ATK_MC2640_USING_DCMI == 0)
/**
 * @brief       初始化ATK-MC2640模块DMA
 * @param       meminc          : DMA_MINC_DISABLE: 帧数据接收的目的地址自动增加
 *                                DMA_MINC_ENABLE : 帧数据接收的目的地址不自动增加
 *              memdataalignment: DMA_MDATAALIGN_BYTE    : 帧数据接收缓冲的位宽为8比特
 *                                DMA_MDATAALIGN_HALFWORD: 帧数据接收缓冲的位宽为16比特
 *                                DMA_MDATAALIGN_WORD    : 帧数据接收缓冲的位宽为32比特
 * @retval      无
 */
static void atk_mc2640_dma_init(uint32_t meminc, uint32_t memdataalignment)
{
    ATK_MC2640_DMA_CLK_ENABLE();
    
    g_atk_mc2640_sta.read.dma_handle.Instance                 = ATK_MC2640_DMA_INTERFACE;
    g_atk_mc2640_sta.read.dma_handle.Init.Channel             = ATK_MC2640_DMA_CHANNEL;
    g_atk_mc2640_sta.read.dma_handle.Init.Direction           = DMA_MEMORY_TO_MEMORY;
    g_atk_mc2640_sta.read.dma_handle.Init.PeriphInc           = DMA_PINC_ENABLE;
    g_atk_mc2640_sta.read.dma_handle.Init.MemInc              = meminc;
    g_atk_mc2640_sta.read.dma_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    g_atk_mc2640_sta.read.dma_handle.Init.MemDataAlignment    = memdataalignment;
    g_atk_mc2640_sta.read.dma_handle.Init.Mode                = DMA_NORMAL;
    g_atk_mc2640_sta.read.dma_handle.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
    
    HAL_DMA_Init(&g_atk_mc2640_sta.read.dma_handle);
}
#endif

/**
 * @brief       初始化ATK-MC2640模块
 * @param       无
 * @retval      ATK_MC2640_EOK   : ATK-MC2640模块初始化成功
 *              ATK_MC2640_ERROR : 通讯出错，ATK-MC2640模块初始化失败
 *              ATK_MC2640_ENOMEM: 内存不足，ATK-MC2640模块初始化失败
 */
uint8_t atk_mc2640_init(void)
{
    uint16_t mid;
    uint16_t pid;
    
    atk_mc2640_hw_init();           /* ATK-MC2640模块硬件初始化 */
    atk_mc2640_exit_power_down();   /* ATK-MC2640模块退出掉电模式 */
    atk_mc2640_hw_reset();          /* ATK-MC2640模块硬件复位 */
    atk_mc2640_sccb_init();         /* ATK-MC2640 SCCB接口初始化 */
    atk_mc2640_sw_reset();          /* ATK-MC2640模块软件复位 */
    
    mid = atk_mc2640_get_mid();     /* 获取制造商ID */
    if (mid != ATK_MC2640_MID)
    {
        return ATK_MC2640_ERROR;
    }
    
    pid = atk_mc2640_get_pid();     /* 获取产品ID */
    if (pid != ATK_MC2640_PID)
    {
        return ATK_MC2640_ERROR;
    }
    
    atk_mc2640_init_reg();          /* 初始化ATK-MC2640寄存器配置 */
    
#if (ATK_MC2640_USING_DCMI == 0)
    g_atk_mc2640_sta.read.line_buf = mymalloc(SRAMIN, g_atk_mc2640_sta.output.width * sizeof(uint16_t));
    
    if (g_atk_mc2640_sta.read.line_buf == NULL)
    {
        return ATK_MC2640_ENOMEM;
    }
#endif
    
#if (ATK_MC2640_USING_DCMI != 0)
    atk_mc2640_dcmi_init();         /* 初始化ATK-MC2640模块DCMI接口 */
#endif
    
    return ATK_MC2640_EOK;
}

#if (ATK_MC2640_LED_CTL_BY_OV2640 == 0)
/**
 * @brief       开启ATK-MC2640模块闪光灯
 * @param       无
 * @retval      无
 */
void atk_mc2640_led_on(void)
{
    ATK_MC2640_FLASH(1);
}

/**
 * @brief       关闭ATK-MC2640模块闪光灯
 * @param       无
 * @retval      无
 */
void atk_mc2640_led_off(void)
{
    ATK_MC2640_FLASH(0);
}
#else
/**
 * @brief       闪烁ATK-MC2640模块控制LED
 * @param       无
 * @retval      无
 */
void atk_mc2640_led_enable(void)
{
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_SENSOR);
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_COM22, 0x81);
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_COM22, 0x01);
}
#endif

/**
 * @brief       设置ATK-MC2640模块灯光模式
 * @param       mode: ATK_MC2640_LIGHT_MOED_AUTO  : Auto
 *                    ATK_MC2640_LIGHT_MOED_SUNNY : Sunny
 *                    ATK_MC2640_LIGHT_MOED_CLOUDY: Cloudy
 *                    ATK_MC2640_LIGHT_MOED_OFFICE: Office
 *                    ATK_MC2640_LIGHT_MOED_HOME  : Home
 * @retval      ATK_MC2640_EOK   : 设置ATK-MC2640模块灯光模式成功
 *              ATK_MC2640_EINVAL: 传入参数错误
 */
uint8_t atk_mc2640_set_light_mode(atk_mc2640_light_mode_t mode)
{
    switch (mode)
    {
        case ATK_MC2640_LIGHT_MODE_AUTO:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0xC7, 0x00);
            break;
        }
        case ATK_MC2640_LIGHT_MODE_SUNNY:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0xC7, 0x40);
            atk_mc2640_write_reg(0xCC, 0x5E);
            atk_mc2640_write_reg(0xCD, 0x41);
            atk_mc2640_write_reg(0xCE, 0x54);
            break;
        }
        case ATK_MC2640_LIGHT_MODE_CLOUDY:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0xC7, 0x40);
            atk_mc2640_write_reg(0xCC, 0x65);
            atk_mc2640_write_reg(0xCD, 0x41);
            atk_mc2640_write_reg(0xCE, 0x4F);
            break;
        }
        case ATK_MC2640_LIGHT_MODE_OFFICE:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0xC7, 0x40);
            atk_mc2640_write_reg(0xCC, 0x52);
            atk_mc2640_write_reg(0xCD, 0x41);
            atk_mc2640_write_reg(0xCE, 0x66);
            break;
        }
        case ATK_MC2640_LIGHT_MODE_HOME:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0xC7, 0x40);
            atk_mc2640_write_reg(0xCC, 0x42);
            atk_mc2640_write_reg(0xCD, 0x3F);
            atk_mc2640_write_reg(0xCE, 0x71);
            break;
        }
        default:
        {
            return ATK_MC2640_EINVAL;
        }
    }
    
    return ATK_MC2640_EOK;
}

/**
 * @brief       设置ATK-MC2640模块色彩饱和度
 * @param       saturation: ATK_MC2640_COLOR_SATURATION_0: +2
 *                          ATK_MC2640_COLOR_SATURATION_1: +1
 *                          ATK_MC2640_COLOR_SATURATION_2: 0
 *                          ATK_MC2640_COLOR_SATURATION_3: -1
 *                          ATK_MC2640_COLOR_SATURATION_4: -2
 * @retval      ATK_MC2640_EOK   : 设置ATK-MC2640模块色彩饱和度成功
 *              ATK_MC2640_EINVAL: 传入参数错误
 */
uint8_t atk_mc2640_set_color_saturation(atk_mc2640_color_saturation_t saturation)
{
    switch (saturation)
    {
        case ATK_MC2640_COLOR_SATURATION_0:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x02);
            atk_mc2640_write_reg(0x7C, 0x03);
            atk_mc2640_write_reg(0x7D, 0x68);
            atk_mc2640_write_reg(0x7D, 0x68);
            break;
        }
        case ATK_MC2640_COLOR_SATURATION_1:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x02);
            atk_mc2640_write_reg(0x7C, 0x03);
            atk_mc2640_write_reg(0x7D, 0x58);
            atk_mc2640_write_reg(0x7D, 0x58);
            break;
        }
        case ATK_MC2640_COLOR_SATURATION_2:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x02);
            atk_mc2640_write_reg(0x7C, 0x03);
            atk_mc2640_write_reg(0x7D, 0x48);
            atk_mc2640_write_reg(0x7D, 0x48);
            break;
        }
        case ATK_MC2640_COLOR_SATURATION_3:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x02);
            atk_mc2640_write_reg(0x7C, 0x03);
            atk_mc2640_write_reg(0x7D, 0x38);
            atk_mc2640_write_reg(0x7D, 0x38);
            break;
        }
        case ATK_MC2640_COLOR_SATURATION_4:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x02);
            atk_mc2640_write_reg(0x7C, 0x03);
            atk_mc2640_write_reg(0x7D, 0x28);
            atk_mc2640_write_reg(0x7D, 0x28);
            break;
        }
        default:
        {
            return ATK_MC2640_EINVAL;
        }
    }
    
    return ATK_MC2640_EOK;
}

/**
 * @brief       设置ATK-MC2640模块亮度
 * @param       brightness: ATK_MC2640_BRIGHTNESS_0: +2
 *                          ATK_MC2640_BRIGHTNESS_1: +1
 *                          ATK_MC2640_BRIGHTNESS_2: 0
 *                          ATK_MC2640_BRIGHTNESS_3: -1
 *                          ATK_MC2640_BRIGHTNESS_4: -2
 * @retval      ATK_MC2640_EOK   : 设置ATK-MC2640模块亮度成功
 *              ATK_MC2640_EINVAL: 传入参数错误
 */
uint8_t atk_mc2640_set_brightness(atk_mc2640_brightness_t brightness)
{
    switch (brightness)
    {
        case ATK_MC2640_BRIGHTNESS_0:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x04);
            atk_mc2640_write_reg(0x7C, 0x09);
            atk_mc2640_write_reg(0x7D, 0x40);
            atk_mc2640_write_reg(0x7D, 0x00);
            break;
        }
        case ATK_MC2640_BRIGHTNESS_1:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x04);
            atk_mc2640_write_reg(0x7C, 0x09);
            atk_mc2640_write_reg(0x7D, 0x30);
            atk_mc2640_write_reg(0x7D, 0x00);
            break;
        }
        case ATK_MC2640_BRIGHTNESS_2:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x04);
            atk_mc2640_write_reg(0x7C, 0x09);
            atk_mc2640_write_reg(0x7D, 0x20);
            atk_mc2640_write_reg(0x7D, 0x00);
            break;
        }
        case ATK_MC2640_BRIGHTNESS_3:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x04);
            atk_mc2640_write_reg(0x7C, 0x09);
            atk_mc2640_write_reg(0x7D, 0x10);
            atk_mc2640_write_reg(0x7D, 0x00);
            break;
        }
        case ATK_MC2640_BRIGHTNESS_4:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x04);
            atk_mc2640_write_reg(0x7C, 0x09);
            atk_mc2640_write_reg(0x7D, 0x00);
            atk_mc2640_write_reg(0x7D, 0x00);
            break;
        }
        default:
        {
            return ATK_MC2640_EINVAL;
        }
    }
    
    return ATK_MC2640_EOK;
}

/**
 * @brief       设置ATK-MC2640模块对比度
 * @param       contrast: ATK_MC2640_CONTRAST_0: +2
 *                        ATK_MC2640_CONTRAST_1: +1
 *                        ATK_MC2640_CONTRAST_2: 0
 *                        ATK_MC2640_CONTRAST_3: -1
 *                        ATK_MC2640_CONTRAST_4: -2
 * @retval      ATK_MC2640_EOK   : 设置ATK-MC2640模块对比度成功
 *              ATK_MC2640_EINVAL: 传入参数错误
 */
uint8_t atk_mc2640_set_contrast(atk_mc2640_contrast_t contrast)
{
    switch (contrast)
    {
        case ATK_MC2640_CONTRAST_0:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x04);
            atk_mc2640_write_reg(0x7C, 0x07);
            atk_mc2640_write_reg(0x7D, 0x20);
            atk_mc2640_write_reg(0x7D, 0x28);
            atk_mc2640_write_reg(0x7D, 0x0C);
            atk_mc2640_write_reg(0x7D, 0x06);
            break;
        }
        case ATK_MC2640_CONTRAST_1:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x04);
            atk_mc2640_write_reg(0x7C, 0x07);
            atk_mc2640_write_reg(0x7D, 0x20);
            atk_mc2640_write_reg(0x7D, 0x24);
            atk_mc2640_write_reg(0x7D, 0x16);
            atk_mc2640_write_reg(0x7D, 0x06);
            break;
        }
        case ATK_MC2640_CONTRAST_2:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x04);
            atk_mc2640_write_reg(0x7C, 0x07);
            atk_mc2640_write_reg(0x7D, 0x20);
            atk_mc2640_write_reg(0x7D, 0x20);
            atk_mc2640_write_reg(0x7D, 0x20);
            atk_mc2640_write_reg(0x7D, 0x06);
            break;
        }
        case ATK_MC2640_CONTRAST_3:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x04);
            atk_mc2640_write_reg(0x7C, 0x07);
            atk_mc2640_write_reg(0x7D, 0x20);
            atk_mc2640_write_reg(0x7D, 0x1C);
            atk_mc2640_write_reg(0x7D, 0x2A);
            atk_mc2640_write_reg(0x7D, 0x06);
            break;
        }
        case ATK_MC2640_CONTRAST_4:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x04);
            atk_mc2640_write_reg(0x7C, 0x07);
            atk_mc2640_write_reg(0x7D, 0x20);
            atk_mc2640_write_reg(0x7D, 0x18);
            atk_mc2640_write_reg(0x7D, 0x34);
            atk_mc2640_write_reg(0x7D, 0x06);
            break;
        }
        default:
        {
            return ATK_MC2640_EINVAL;
        }
    }
    
    return ATK_MC2640_EOK;
}


/**
 * @brief       设置ATK-MC2640模块特殊效果
 * @param       contrast: ATK_MC2640_SPECIAL_EFFECT_ANTIQUE    : Antique
 *                        ATK_MC2640_SPECIAL_EFFECT_BLUISH     : Bluish
 *                        ATK_MC2640_SPECIAL_EFFECT_GREENISH   : Greenish
 *                        ATK_MC2640_SPECIAL_EFFECT_REDISH     : Redish
 *                        ATK_MC2640_SPECIAL_EFFECT_BW         : B&W
 *                        ATK_MC2640_SPECIAL_EFFECT_NEGATIVE   : Negative
 *                        ATK_MC2640_SPECIAL_EFFECT_BW_NEGATIVE: B&W Negative
 *                        ATK_MC2640_SPECIAL_EFFECT_NORMAL     : Normal
 * @retval      ATK_MC2640_EOK   : 设置ATK-MC2640模块特殊效果成功
 *              ATK_MC2640_EINVAL: 传入参数错误
 */
uint8_t atk_mc2640_set_special_effect(atk_mc2640_special_effect_t effect)
{
    switch (effect)
    {
        case ATK_MC2640_SPECIAL_EFFECT_ANTIQUE:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x18);
            atk_mc2640_write_reg(0x7C, 0x05);
            atk_mc2640_write_reg(0x7D, 0x40);
            atk_mc2640_write_reg(0x7D, 0xA6);
            break;
        }
        case ATK_MC2640_SPECIAL_EFFECT_BLUISH:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x18);
            atk_mc2640_write_reg(0x7C, 0x05);
            atk_mc2640_write_reg(0x7D, 0xA0);
            atk_mc2640_write_reg(0x7D, 0x40);
            break;
        }
        case ATK_MC2640_SPECIAL_EFFECT_GREENISH:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x18);
            atk_mc2640_write_reg(0x7C, 0x05);
            atk_mc2640_write_reg(0x7D, 0x40);
            atk_mc2640_write_reg(0x7D, 0x40);
            break;
        }
        case ATK_MC2640_SPECIAL_EFFECT_REDISH:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x18);
            atk_mc2640_write_reg(0x7C, 0x05);
            atk_mc2640_write_reg(0x7D, 0x40);
            atk_mc2640_write_reg(0x7D, 0xC0);
            break;
        }
        case ATK_MC2640_SPECIAL_EFFECT_BW:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x18);
            atk_mc2640_write_reg(0x7C, 0x05);
            atk_mc2640_write_reg(0x7D, 0x80);
            atk_mc2640_write_reg(0x7D, 0x80);
            break;
        }
        case ATK_MC2640_SPECIAL_EFFECT_NEGATIVE:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x40);
            atk_mc2640_write_reg(0x7C, 0x05);
            atk_mc2640_write_reg(0x7D, 0x80);
            atk_mc2640_write_reg(0x7D, 0x80);
            break;
        }
        case ATK_MC2640_SPECIAL_EFFECT_BW_NEGATIVE:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x58);
            atk_mc2640_write_reg(0x7C, 0x05);
            atk_mc2640_write_reg(0x7D, 0x80);
            atk_mc2640_write_reg(0x7D, 0x80);
            break;
        }
        case ATK_MC2640_SPECIAL_EFFECT_NORMAL:
        {
            atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
            atk_mc2640_write_reg(0x7C, 0x00);
            atk_mc2640_write_reg(0x7D, 0x00);
            atk_mc2640_write_reg(0x7C, 0x05);
            atk_mc2640_write_reg(0x7D, 0x80);
            atk_mc2640_write_reg(0x7D, 0x80);
            break;
        }
        default:
        {
            return ATK_MC2640_EINVAL;
        }
    }
    
    return ATK_MC2640_EOK;
}

/**
 * @brief       设置ATK-MC2640模块输出图像格式
 * @param       mode: ATK_MC2640_OUTPUT_FORMAT_RGB565: RGB565
 *                    ATK_MC2640_OUTPUT_FORMAT_JPEG : JPEG
 * @retval      ATK_MC2640_EOK   : 设置ATK-MC2640模块输出图像格式成功
 *              ATK_MC2640_EINVAL: 传入参数错误
 */
uint8_t atk_mc2640_set_output_format(atk_mc2640_output_format_t format)
{
    uint32_t cfg_index;
    
    switch (format)
    {
        case ATK_MC2640_OUTPUT_FORMAT_RGB565:
        {
            for (cfg_index=0; cfg_index<(sizeof(atk_mc2640_set_rgb565_cfg)/sizeof(atk_mc2640_set_rgb565_cfg[0])); cfg_index++)
            {
                atk_mc2640_write_reg(atk_mc2640_set_rgb565_cfg[cfg_index][0], atk_mc2640_set_rgb565_cfg[cfg_index][1]);
            }
            break;
        }
        case ATK_MC2640_OUTPUT_FORMAT_JPEG:
        {
            for (cfg_index=0; cfg_index<(sizeof(atk_mc2640_set_yuv422_cfg)/sizeof(atk_mc2640_set_yuv422_cfg[0])); cfg_index++)
            {
                atk_mc2640_write_reg(atk_mc2640_set_yuv422_cfg[cfg_index][0], atk_mc2640_set_yuv422_cfg[cfg_index][1]);
            }
            for (cfg_index=0; cfg_index<(sizeof(atk_mc2640_set_jpeg_cfg)/sizeof(atk_mc2640_set_jpeg_cfg[0])); cfg_index++)
            {
                atk_mc2640_write_reg(atk_mc2640_set_jpeg_cfg[cfg_index][0], atk_mc2640_set_jpeg_cfg[cfg_index][1]);
            }
            break;
        }
        default:
        {
            return ATK_MC2640_EINVAL;
        }
    }
    
    return ATK_MC2640_EOK;
}

/**
 * @brief       设置ATK-MC2640模块输出图像分辨率
 * @param       width : 输出图像宽度，必须是4的倍数
 *              height: 输出图像高度，必须是4的倍数
 * @retval      ATK_MC2640_EOK   : 设置ATK-MC2640模块输出图像大小成功
 *              ATK_MC2640_EINVAL: 传入参数错误
 *              ATK_MC2640_ENOMEM: 内存不足
 */
uint8_t atk_mc2640_set_output_size(uint16_t width, uint16_t height)
{
    uint16_t output_width;
    uint16_t output_height;
    
    if (((width & (4 - 1)) != 0) || ((height & (4 - 1)) != 0))
    {
        return ATK_MC2640_EINVAL;
    }
    
#if (ATK_MC2640_USING_DCMI == 0)
    myfree(SRAMIN, g_atk_mc2640_sta.read.line_buf);
    g_atk_mc2640_sta.read.line_buf = mymalloc(SRAMIN, width * sizeof(uint16_t));
    if (g_atk_mc2640_sta.read.line_buf == NULL)
    {
        g_atk_mc2640_sta.read.line_buf = mymalloc(SRAMIN, g_atk_mc2640_sta.output.width * sizeof(uint16_t));
        return ATK_MC2640_ENOMEM;
    }
    else
#endif
    {
        g_atk_mc2640_sta.output.width = width;
        g_atk_mc2640_sta.output.height = height;
    }
    
    output_width = width >> 2;
    output_height = height >> 2;
    
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_RESET, 0x04);
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_ZMOW, (uint8_t)(output_width & 0x00FF));
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_ZMOH, (uint8_t)(output_height & 0x00FF));
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_ZMHH, ((uint8_t)(output_width >> 8) & 0x03) | ((uint8_t)(output_height >> 6) & 0x04));
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_RESET, 0x00);
    
    return ATK_MC2640_EOK;
}

/**
 * @brief       设置ATK-MC2640模块传感器窗口
 * @param       start_x: 传感器窗口起始X坐标
 *              start_y: 传感器窗口起始Y坐标
 *              width  : 传感器窗口宽度
 *              height : 传感器窗口高度
 * @retval      无
 */
void atk_mc2640_set_sensor_window(uint16_t start_x, uint16_t start_y, uint16_t width, uint16_t height)
{
    uint16_t end_x;
    uint16_t end_y;
    uint8_t raw_com1;
    uint8_t com1;
    uint8_t raw_reg32;
    uint8_t reg32;
    
    end_x = start_x + (width >> 1);
    end_y = start_y + (height >> 1);
    
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_SENSOR);
    
    raw_com1 = atk_mc2640_read_reg(ATK_MC2640_REG_SENSOR_COM1);
    com1 = (raw_com1 & 0xF0) | (((end_y & 0x03) << 2) | (start_y & 0x03));
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_COM1, com1);
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_VSTRT, start_y >> 2);
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_VEND, end_y >> 2);
    
    raw_reg32 = atk_mc2640_read_reg(ATK_MC2640_REG_SENSOR_REG32);
    reg32 = (raw_reg32 & 0xC0) | (((end_x & 0x07) << 3) | (start_x & 0x07));
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_REG32, reg32);
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_HREFST, start_x >> 3);
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_HREFEND, end_x >> 3);
}

/**
 * @brief       设置ATK-MC2640模块输出图像窗口
 * @param       off_x : 输出图像窗口偏移X坐标
 *              off_y : 输出图像窗口偏移Y坐标
 *              width : 输出图像窗口宽度，必须是4的倍数
 *              height: 输出图像窗口高度，必须是4的倍数
 * @retval      ATK_MC2640_EOK   : 设置ATK-MC2640模块输出图像大小成功
 *              ATK_MC2640_EINVAL: 传入参数错误
 */
uint8_t atk_mc2640_set_image_window(uint16_t off_x, uint16_t off_y, uint16_t width, uint16_t height)
{
    uint16_t hsize;
    uint16_t vsize;
    uint8_t vhyx;
    
    if (((width & (4 - 1)) != 0) || ((height & (4 - 1)) != 0))
    {
        return ATK_MC2640_EINVAL;
    }
    
    hsize = width >> 2;
    vsize = height >> 2;
    
    vhyx = (uint8_t)(((vsize >> 1) & 0x80) | ((off_y >> 4) & 0x70) | ((hsize >> 5) & 0x08) | ((off_x >> 8) & 0x07));
    
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_RESET, 0x04);
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_HSIZE, (uint8_t)(hsize & 0x00FF));
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_VSIZE, (uint8_t)(vsize & 0x00FF));
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_XOFFL, (uint8_t)(off_x & 0x00FF));
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_YOFFL, (uint8_t)(off_y & 0x00FF));
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_VHYX, vhyx);
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_TEST, (uint8_t)((hsize >> 2) & 0x80));
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_RESET, 0x00);
    
    return ATK_MC2640_EOK;
}

/**
 * @brief       设置ATK-MC2640模块输出图像大小
 * @param       off_x : 输出图像窗口偏移X坐标
 *              off_y : 输出图像窗口偏移Y坐标
 *              width : 输出图像窗口宽度，必须是4的倍数
 *              height: 输出图像窗口高度，必须是4的倍数
 * @retval      ATK_MC2640_EOK   : 设置ATK-MC2640模块输出图像大小成功
 *              ATK_MC2640_EINVAL: 传入参数错误
 */
void atk_mc2640_set_image_size(uint16_t width, uint16_t height)
{
    uint8_t sizel;
    
    sizel = (uint8_t)(((width & 0x07) << 3) | (height & 0x07) | ((width >> 4) & 0x80));
    
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_RESET, 0x04);
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_HSIZE8, (uint8_t)((width >> 3) & 0x00FF));
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_VSIZE8, (uint8_t)((height >> 3) & 0x00FF));
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_SIZEL, sizel);
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_RESET, 0x00);
}

/**
 * @brief       设置ATK-MC2640模块输出速率
 * @param       clk_dev : Clock divider（0~63）
 *              pclk_dev: DVP PCLK（1~127）
 * @retval      ATK_MC2640_EOK   : 设置ATK-MC2640模块输出速率成功
 *              ATK_MC2640_EINVAL: 传入参数错误
 */
uint8_t atk_mc2640_set_output_speed(uint8_t clk_dev, uint8_t pclk_dev)
{
    if (clk_dev > 63)
    {
        return ATK_MC2640_EINVAL;
    }
    
    if ((pclk_dev == 0) || (pclk_dev > 127))
    {
        return ATK_MC2640_EINVAL;
    }
    
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_DSP);
    atk_mc2640_write_reg(ATK_MC2640_REG_DSP_R_DVP_SP, pclk_dev);
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_SENSOR);
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_CLKRC, clk_dev);
    
    return ATK_MC2640_EOK;
}

/**
 * @brief       开启ATK-MC2640模块彩条测试
 * @param       无
 * @retval      无
 */
void atk_mc2640_colorbar_enable(void)
{
    uint8_t com7;
    
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_SENSOR);
    com7 = atk_mc2640_read_reg(ATK_MC2640_REG_SENSOR_COM7);
    com7 |= (uint8_t)(1 << 1);
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_COM7, com7);
}

/**
 * @brief       关闭ATK-MC2640模块彩条测试
 * @param       无
 * @retval      无
 */
void atk_mc2640_colorbar_disable(void)
{
    uint8_t com7;
    
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_SENSOR);
    com7 = atk_mc2640_read_reg(ATK_MC2640_REG_SENSOR_COM7);
    com7 &= ~(uint8_t)(1 << 1);
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_COM7, com7);
}

/**
 * @brief       获取ATK-MC2640模块输出的一帧图像数据
 * @param       dts_addr : 帧数据的接收缓冲的首地址
 *              type: ATK_MC2640_GET_TYPE_DTS_8B_NOINC : 图像数据以字节方式写入目的地址，目的地址固定不变
 *                    ATK_MC2640_GET_TYPE_DTS_8B_INC   : 图像数据以字节方式写入目的地址，目的地址自动增加
 *                    ATK_MC2640_GET_TYPE_DTS_16B_NOINC: 图像数据以半字方式写入目的地址，目的地址固定不变
 *                    ATK_MC2640_GET_TYPE_DTS_16B_INC  : 图像数据以半字方式写入目的地址，目的地址自动增加
 *                    ATK_MC2640_GET_TYPE_DTS_32B_NOINC: 图像数据以字方式写入目的地址，目的地址固定不变
 *                    ATK_MC2640_GET_TYPE_DTS_32B_INC  : 图像数据以字方式写入目的地址，目的地址自动增加
 *              before_transfer: 帧数据传输前，需要完成的事务，可为NULL
 * @retval      ATK_MC2640_EOK   : 获取ATK-MC2640模块输出的一帧图像数据成功
 *              ATK_MC2640_EINVAL: 传入参数错误
 *              ATK_MC2640_EEMPTY: 图像数据为空
 */
uint8_t atk_mc2640_get_frame(uint32_t dts_addr, atk_mc2640_get_type_t type, void (*before_transfer)(void))
{
    uint32_t meminc;
    uint32_t memdataalignment;
#if (ATK_MC2640_USING_DCMI != 0)
    uint32_t len;
#else
    uint16_t pixel_cnt = 0;
    uint16_t line_cnt = 0;
    uint16_t dts_offset;
#endif
    
    switch (type)
    {
        case ATK_MC2640_GET_TYPE_DTS_8B_NOINC:
        {
            meminc = DMA_MINC_DISABLE;
            memdataalignment = DMA_MDATAALIGN_BYTE;
#if (ATK_MC2640_USING_DCMI != 0)
            len = (g_atk_mc2640_sta.output.width * g_atk_mc2640_sta.output.height) / sizeof(uint8_t);
#else
            dts_offset = 0;
#endif
            break;
        }
        case ATK_MC2640_GET_TYPE_DTS_8B_INC:
        {
            meminc = DMA_MINC_ENABLE;
            memdataalignment = DMA_MDATAALIGN_BYTE;
#if (ATK_MC2640_USING_DCMI != 0)
            len = (g_atk_mc2640_sta.output.width * g_atk_mc2640_sta.output.height) / sizeof(uint8_t);
#else
            dts_offset = g_atk_mc2640_sta.output.width << 1;
#endif
            break;
        }
        case ATK_MC2640_GET_TYPE_DTS_16B_NOINC:
        {
            meminc = DMA_MINC_DISABLE;
            memdataalignment = DMA_MDATAALIGN_HALFWORD;
#if (ATK_MC2640_USING_DCMI != 0)
            len = (g_atk_mc2640_sta.output.width * g_atk_mc2640_sta.output.height) / sizeof(uint16_t);
					
#else
            dts_offset = 0;
#endif
            break;
        }
        case ATK_MC2640_GET_TYPE_DTS_16B_INC:
        {
            meminc = DMA_MINC_ENABLE;
            memdataalignment = DMA_MDATAALIGN_HALFWORD;
#if (ATK_MC2640_USING_DCMI != 0)
            len = (g_atk_mc2640_sta.output.width * g_atk_mc2640_sta.output.height) / sizeof(uint16_t);
#else
            dts_offset = g_atk_mc2640_sta.output.width << 1;
#endif
            break;
        }
        case ATK_MC2640_GET_TYPE_DTS_32B_NOINC:
        {
            meminc = DMA_MINC_DISABLE;
            memdataalignment = DMA_MDATAALIGN_WORD;
#if (ATK_MC2640_USING_DCMI != 0)
            len = (g_atk_mc2640_sta.output.width * g_atk_mc2640_sta.output.height) / sizeof(uint32_t);
#else
            dts_offset = 0;
#endif
            break;
        }
        case ATK_MC2640_GET_TYPE_DTS_32B_INC:
        {
            meminc = DMA_MINC_ENABLE;
            memdataalignment = DMA_MDATAALIGN_WORD;
#if (ATK_MC2640_USING_DCMI != 0)
            len = (g_atk_mc2640_sta.output.width * g_atk_mc2640_sta.output.height) / sizeof(uint32_t);
#else
            dts_offset = g_atk_mc2640_sta.output.width << 1;
#endif
            break;
        }
        default:
        {
            return ATK_MC2640_EINVAL;
        }
    }
    
    
#if (ATK_MC2640_USING_DCMI != 0)
    if (before_transfer != NULL)
    {
        before_transfer();
    }
    
    atk_mc2640_dcmi_start(dts_addr, meminc, memdataalignment, len);
#else
    atk_mc2640_dma_init(meminc, memdataalignment);
    
    if (ATK_MC2640_READ_VSYNC() != 0)
    {
        return ATK_MC2640_EEMPTY;
    }
    
    if (before_transfer != NULL)
    {
        before_transfer();
    }
    
    while (line_cnt < g_atk_mc2640_sta.output.height)
    {
        while (ATK_MC2640_READ_HREF() != 0)
        {
            while (ATK_MC2640_READ_PCLK() == 0);
            g_atk_mc2640_sta.read.line_buf[pixel_cnt++] = atk_mc2640_get_byte_data();
            while (ATK_MC2640_READ_PCLK() != 0);
            while (ATK_MC2640_READ_PCLK() == 0);
            g_atk_mc2640_sta.read.line_buf[pixel_cnt++] = atk_mc2640_get_byte_data();
            while (ATK_MC2640_READ_PCLK() != 0);
        }
        
        if (pixel_cnt != 0)
        {
            HAL_DMA_Abort(&g_atk_mc2640_sta.read.dma_handle);
            HAL_DMA_Start(&g_atk_mc2640_sta.read.dma_handle, (uint32_t)g_atk_mc2640_sta.read.line_buf, dts_addr, g_atk_mc2640_sta.output.width);
            dts_addr += dts_offset;
            pixel_cnt = 0;
            line_cnt++;
        }
    }
#endif
    
    return ATK_MC2640_EOK;
}

