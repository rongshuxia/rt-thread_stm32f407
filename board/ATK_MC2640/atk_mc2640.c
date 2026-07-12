/*
 * ATK-MC2640 (OV2640) 摄像头驱动。
 *
 * 基于正点原子 ATK-MC2640 例程，本工程配置:
 *   ATK_MC2640_USING_DCMI = 1  — 通过 STM32 DCMI + DMA 采集 DVP 数据
 *   ATK_MC2640_LED_CTL_BY_OV2640 = 1 — 补光灯由传感器寄存器控制
 *
 * 硬件接口:
 *   SCCB (类 I2C) — 传感器寄存器配置，见 atk_mc2640_sccb.c
 *   DVP 8 位并行  — VSYNC/HREF/PCLK + D0-D7，经 DCMI 入 MCU
 *   RST / PWDN    — PG15 / PD3 硬件复位与掉电
 *
 * 数据路径 (DCMI 模式):
 *   OV2640 JPEG 输出 -> DCMI -> DMA -> 用户缓冲 (eth_camera 用 32B_INC)
 *
 * 寄存器表 (UXGA/JPEG/RGB565 等) 见 atk_mc2640_cfg.c。
 */

#include "atk_mc2640.h"
#include "atk_mc2640_sccb.h"
#include "atk_mc2640_cfg.h"
#include "stm32f4xx_hal.h"
#include "stm32f4_delay.h"

#if (ATK_MC2640_USING_DCMI != 0)
#include "atk_mc2640_dcmi.h"
#endif

/* OV2640 制造商 ID / 产品 ID，init 时校验 */
#define ATK_MC2640_MID  0x7FA2
#define ATK_MC2640_PID  0x2642

/* OV2640 寄存器分 Bank: DSP 与 Sensor */
typedef enum
{
    ATK_MC2640_REG_BANK_DSP = 0x00, 
    ATK_MC2640_REG_BANK_SENSOR,     
} atk_mc2640_reg_bank_t;

/* 运行时状态: 当前输出分辨率；非 DCMI 模式另有行缓冲与 DMA */
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

/* ---------- SCCB 寄存器访问 ---------- */

/* 写单字节寄存器 (SCCB 三相写) */
static void atk_mc2640_write_reg(uint8_t reg, uint8_t dat)
{
    atk_mc2640_sccb_3_phase_write(ATK_MC2640_SCCB_ADDR, reg, dat);
}

/* 读单字节寄存器 (SCCB 两相写 + 两相读) */
static uint8_t atk_mc2640_read_reg(uint8_t reg)
{
    uint8_t dat = 0;
    
    atk_mc2640_sccb_2_phase_write(ATK_MC2640_SCCB_ADDR, reg);
    atk_mc2640_sccb_2_phase_read(ATK_MC2640_SCCB_ADDR, &dat);
    
    return dat;
}

/* 切换 DSP / Sensor 寄存器 Bank (REG_BANK_SEL) */
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

/* ---------- 硬件 GPIO ---------- */

/*
 * 初始化 RST/PWDN；非 DCMI 模式下还需配置 D0-D7/VSYNC/HREF/PCLK 为输入。
 * DCMI 模式下 DVP 引脚由 atk_mc2640_dcmi.c 配置。
 */
static void atk_mc2640_hw_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    
    
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
    
    gpio_init_struct.Pin    = ATK_MC2640_VSYNC_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_VSYNC_GPIO_PORT, &gpio_init_struct);
    
    
    gpio_init_struct.Pin    = ATK_MC2640_HREF_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_HREF_GPIO_PORT, &gpio_init_struct);
    
    
    gpio_init_struct.Pin    = ATK_MC2640_D0_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D0_GPIO_PORT, &gpio_init_struct);
    
    
    gpio_init_struct.Pin    = ATK_MC2640_D1_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D1_GPIO_PORT, &gpio_init_struct);
    
    
    gpio_init_struct.Pin    = ATK_MC2640_D2_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D2_GPIO_PORT, &gpio_init_struct);
    
    
    gpio_init_struct.Pin    = ATK_MC2640_D3_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D3_GPIO_PORT, &gpio_init_struct);
    
    
    gpio_init_struct.Pin    = ATK_MC2640_D4_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D4_GPIO_PORT, &gpio_init_struct);
    
    
    gpio_init_struct.Pin    = ATK_MC2640_D5_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D5_GPIO_PORT, &gpio_init_struct);
    
    
    gpio_init_struct.Pin    = ATK_MC2640_D6_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D6_GPIO_PORT, &gpio_init_struct);
    
    
    gpio_init_struct.Pin    = ATK_MC2640_D7_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_D7_GPIO_PORT, &gpio_init_struct);
    
    
    gpio_init_struct.Pin    = ATK_MC2640_PCLK_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_PCLK_GPIO_PORT, &gpio_init_struct);
#endif
    
    
    gpio_init_struct.Pin    = ATK_MC2640_RST_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_RST_GPIO_PORT, &gpio_init_struct);
    
    
    gpio_init_struct.Pin    = ATK_MC2640_PWDN_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_PWDN_GPIO_PORT, &gpio_init_struct);
    
#if (ATK_MC2640_LED_CTL_BY_OV2640 == 0)
    
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

/* PWDN 拉低，退出掉电 */
static void atk_mc2640_exit_power_down(void)
{
    ATK_MC2640_PWDN(0);
    stm32f4_delay_ms(10);
}

/* RST 低脉冲硬件复位 */
static void atk_mc2640_hw_reset(void)
{
    ATK_MC2640_RST(0);
    stm32f4_delay_ms(10);
    ATK_MC2640_RST(1);
    stm32f4_delay_ms(10);
}

/* 写 COM7 bit7 软件复位 Sensor Bank */
static void atk_mc2640_sw_reset(void)
{
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_SENSOR);
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_COM7, 0x80);
    stm32f4_delay_ms(50);
}

/* 读取 MIDH/MIDL 组成 16 位制造商 ID */
static uint16_t atk_mc2640_get_mid(void)
{
    uint16_t mid;
    
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_SENSOR);
    mid = atk_mc2640_read_reg(ATK_MC2640_REG_SENSOR_MIDH) << 8;
    mid |= atk_mc2640_read_reg(ATK_MC2640_REG_SENSOR_MIDL);
    
    return mid;
}

/* 读取 PIDH/PIDL 组成 16 位产品 ID (期望 0x2642) */
static uint16_t atk_mc2640_get_pid(void)
{
    uint16_t pid;
    
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_SENSOR);
    pid = atk_mc2640_read_reg(ATK_MC2640_REG_SENSOR_PIDH) << 8;
    pid |= atk_mc2640_read_reg(ATK_MC2640_REG_SENSOR_PIDL);
    
    return pid;
}

/*
 * 加载 UXGA 初始化寄存器表，并从 ZMOW/ZMOH/ZMHH 解析默认输出尺寸。
 * 寄存器数组定义在 atk_mc2640_cfg.c。
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

/* ---------- GPIO 位 bang 采帧 (ATK_MC2640_USING_DCMI==0) ---------- */

#if (ATK_MC2640_USING_DCMI == 0)

/* 从 D0-D7 GPIO 拼一字节像素数据 */
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

/* 配置 DMA 将行缓冲搬运到目标地址 (内存到内存) */
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

/* ---------- 公开 API: 初始化 ---------- */

/*
 * 完整初始化: GPIO -> 退出掉电 -> 硬复位 -> SCCB -> 软复位 -> ID 校验 -> 寄存器表 -> DCMI。
 * @return ATK_MC2640_EOK / ATK_MC2640_ERROR (ID 不匹配) / ATK_MC2640_ENOMEM
 */
uint8_t atk_mc2640_init(void)
{
    uint16_t mid;
    uint16_t pid;
    
    atk_mc2640_hw_init();
    atk_mc2640_exit_power_down();
    atk_mc2640_hw_reset();
    atk_mc2640_sccb_init();
    atk_mc2640_sw_reset();

    mid = atk_mc2640_get_mid();
    if (mid != ATK_MC2640_MID)
    {
        return ATK_MC2640_ERROR;
    }

    pid = atk_mc2640_get_pid();
    if (pid != ATK_MC2640_PID)
    {
        return ATK_MC2640_ERROR;
    }

    atk_mc2640_init_reg();

#if (ATK_MC2640_USING_DCMI == 0)
    g_atk_mc2640_sta.read.line_buf = mymalloc(SRAMIN, g_atk_mc2640_sta.output.width * sizeof(uint16_t));
    
    if (g_atk_mc2640_sta.read.line_buf == NULL)
    {
        return ATK_MC2640_ENOMEM;
    }
#endif
    
#if (ATK_MC2640_USING_DCMI != 0)
    atk_mc2640_dcmi_init();         
#endif
    
    return ATK_MC2640_EOK;
}

/* ---------- 补光灯 ---------- */

#if (ATK_MC2640_LED_CTL_BY_OV2640 == 0)

void atk_mc2640_led_on(void)
{
    ATK_MC2640_FLASH(1);
}

void atk_mc2640_led_off(void)
{
    ATK_MC2640_FLASH(0);
}
#else

/* 通过 OV2640 COM22 寄存器使能模块 LED */
void atk_mc2640_led_enable(void)
{
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_SENSOR);
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_COM22, 0x81);
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_COM22, 0x01);
}
#endif

/* ---------- 图像效果 (均写 DSP Bank 寄存器) ---------- */

/* 白平衡/光线模式: Auto / Sunny / Cloudy / Office / Home */
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

/* 色彩饱和度 (+2 ~ -2) */
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

/* 亮度 (+2 ~ -2) */
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

/* 对比度 (+2 ~ -2) */
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

/* 特效: 复古/偏色/黑白/负片/正常等 */
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

/* ---------- 输出格式与尺寸 ---------- */

/* 切换 RGB565 或 JPEG (JPEG 需先 YUV422 再 JPEG 寄存器组) */
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

/* 设置输出分辨率，宽高须 4 像素对齐；写 DSP ZMOW/ZMOH/ZMHH */
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

/* 设置 Sensor 侧原始感光窗口 (裁剪传感器阵列) */
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

/* 设置 DSP 侧输出窗口偏移与大小 (HSIZE/VSIZE/XOFF/YOFF) */
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

/* 设置 DSP 输出图像尺寸 (HSIZE8/VSIZE8/SIZEL) */
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

/* 调整 PCLK 分频: clk_dev (Sensor) + pclk_dev (DSP DVP) */
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

/* ---------- 调试 ---------- */

/* 开启传感器内置彩条测试图 (COM7 bit1) */
void atk_mc2640_colorbar_enable(void)
{
    uint8_t com7;
    
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_SENSOR);
    com7 = atk_mc2640_read_reg(ATK_MC2640_REG_SENSOR_COM7);
    com7 |= (uint8_t)(1 << 1);
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_COM7, com7);
}

/* 关闭彩条 */
void atk_mc2640_colorbar_disable(void)
{
    uint8_t com7;
    
    atk_mc2640_reg_bank_select(ATK_MC2640_REG_BANK_SENSOR);
    com7 = atk_mc2640_read_reg(ATK_MC2640_REG_SENSOR_COM7);
    com7 &= ~(uint8_t)(1 << 1);
    atk_mc2640_write_reg(ATK_MC2640_REG_SENSOR_COM7, com7);
}

/* ---------- 帧采集 ---------- */

/* 采集一帧，不限制缓冲大小 (buf_bytes=0) */
uint8_t atk_mc2640_get_frame(uint32_t dts_addr, atk_mc2640_get_type_t type, void (*before_transfer)(void))
{
    return atk_mc2640_get_frame_sized(dts_addr, type, before_transfer, 0);
}

/*
 * 采集一帧到 dts_addr，可限制最大字节数 buf_bytes。
 *
 * @param type  DMA 目标宽度与地址递增方式 (8/16/32 bit, INC/NOINC)
 *              eth_camera 使用 DTS_32B_INC: 32 位字写入且地址递增
 * @param before_transfer  开始 DMA 前回调 (可为 NULL)
 * @param buf_bytes        >0 时按 uint32_t 字数截断传输长度 (JPEG 防溢出)
 *
 * DCMI 模式: atk_mc2640_dcmi_start() 阻塞至帧完成或超时 (dcmi_sem)。
 * GPIO 模式: 逐 PCLK 读 D0-D7，行 DMA 搬运 (本工程未启用)。
 */
uint8_t atk_mc2640_get_frame_sized(uint32_t dts_addr, atk_mc2640_get_type_t type, void (*before_transfer)(void), uint32_t buf_bytes)
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

    /* 按 buf_bytes 限制 DMA 字数，防止 JPEG 超出用户缓冲 */
    if (buf_bytes >= sizeof(uint32_t))
    {
        uint32_t max_words = buf_bytes / sizeof(uint32_t);

        if (len > max_words)
        {
            len = max_words;
        }
    }

    if (atk_mc2640_dcmi_start(dts_addr, meminc, memdataalignment, len) != 0)
    {
        return ATK_MC2640_EEMPTY;
    }
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

