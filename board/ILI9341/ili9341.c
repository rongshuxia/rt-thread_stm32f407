/*
 * ILI9341 TFT-LCD 驱动 — 16 位 FSMC 并行 8080 接口，竖屏 240×320。
 *
 * 芯片: ILI9341 (320×240 物理像素，RGB565 16bpp)
 * 板卡: 启明欣欣 STM32F407ZG 开发板(高配版)
 *
 * 8080 并口通过 FSMC 模拟:
 *   Bank1 NE4 / PG12  -> LCD 片选 CS#
 *   FSMC_A12 / PG2    -> RS (0=命令寄存器, 1=GRAM 数据)
 *   FSMC_D0-D15       -> 16 位数据 (与外部 SRAM 共享)
 *   FSMC_NOE/NWE      -> 读/写 strobe
 *
 * 内存映射 (见 ili9341.h):
 *   0x6C000000        ILI9341_REG — 写命令 (RS=0)
 *   0x6C002000        ILI9341_RAM — 写像素/参数 (RS=1, A12 偏移 0x2000)
 *
 * 依赖关系:
 *   外部 SRAM (is62wv51216) 在 board 阶段已初始化完整 FSMC D/A 总线;
 *   本驱动额外配置 NE4、A12 以及背光/触摸 GPIO，并单独初始化 Bank4 时序。
 *
 * LVGL 刷新路径: lv_port.c -> ili9341_flush_area() -> FSMC 批量写 GRAM。
 */

#include "ili9341.h"
#include <board.h>
#include <stm32f4xx_hal.h>
#include "stm32f4_delay.h"

extern void rt_hw_us_delay(rt_uint32_t us);

/*
 * 毫秒延时，用于 ILI9341 上电/复位时序。
 * 当前使用 rt_thread_mdelay (需调度器已启动，ili9341_init 在 lvgl 线程中调用)。
 * 注释掉的 rt_hw_us_delay 方案可在调度器启动前使用 (如极早期裸机初始化)。
 */
static void delay_ms(uint32_t ms)
{
	rt_thread_mdelay(ms);
	
//    while (ms--)
//    {
//        rt_hw_us_delay(1000);
//    }
}

/* FSMC Bank4 (NE4) 句柄，专用于 LCD 8080 窗口 */
static SRAM_HandleTypeDef hsram_lcd;

/* ---------- 底层 8080 写操作 ---------- */

/* 写命令寄存器 (RS=0, 访问 ILI9341_REG 地址) */
static inline void ili9341_write_reg(uint16_t reg)
{
    ILI9341_REG = reg;
}

/* 写数据/参数 (RS=1, 访问 ILI9341_RAM 地址) */
static inline void ili9341_write_data(uint16_t data)
{
    ILI9341_RAM = data;
}

/* 命令 + 单字节参数，初始化序列常用 */
static inline void ili9341_write_cmd_data(uint16_t reg, uint16_t data)
{
    ILI9341_REG = reg;
    ILI9341_RAM = data;
}

/* ---------- GPIO / FSMC 硬件初始化 ---------- */

/*
 * 配置 LCD 相关 GPIO。
 *
 * 注意: D0-D15、NOE、NWE 等 FSMC 数据线/控制线通常已由 is62wv51216 的
 * fsmc_gpio_init() 初始化; 此处补充 LCD 专用引脚:
 *   PG2  FSMC_A12 -> ILI9341 RS
 *   PG12 FSMC_NE4 -> LCD CS#
 *   PF10 背光 (推挽输出, 高电平点亮)
 *   PF11 触摸 PEN 中断 (输入, 预留未用)
 */
static void ili9341_gpio_init(void)
{
    GPIO_InitTypeDef gi = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* PD0/1/4/5/8/9/10/14/15 -> FSMC 数据 D0-D3,D13-D15 + NOE/NWE */
    gi.Mode      = GPIO_MODE_AF_PP;
    gi.Pull      = GPIO_PULLUP;
    gi.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gi.Alternate = GPIO_AF12_FSMC;
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5
          |  GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10
          |  GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gi);

    /* PE7..PE15 -> FSMC 数据 D4-D12 */
    gi.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10
          |  GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gi);

    /* PG2 (A12/RS), PG12 (NE4/CS#) -> FSMC LCD 片选与寄存器选择 */
    gi.Pin = GPIO_PIN_2 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOG, &gi);

    /* PF10 背光: 推挽输出，初始化时关闭，display on 后再点亮 */
    gi.Mode      = GPIO_MODE_OUTPUT_PP;
    gi.Pull      = GPIO_NOPULL;
    gi.Speed     = GPIO_SPEED_FREQ_LOW;
    gi.Alternate = 0;
    gi.Pin       = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOF, &gi);
    ILI9341_BL_OFF();

    /* PF11 触摸 PEN 中断线: 上拉输入，当前驱动未实现触摸 */
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pull = GPIO_PULLUP;
    gi.Pin  = GPIO_PIN_11;
    HAL_GPIO_Init(GPIOF, &gi);
}

/*
 * 配置 FSMC Bank1 子 Bank4 (NE4) 为 16 位异步 SRAM 模式，驱动 ILI9341 8080 口。
 *
 * LCD 对 FSMC 时序要求较外部 SRAM 宽松，因此 DATAST 比 is62wv51216 更小:
 *   HCLK = 168 MHz, 周期 ≈ 6 ns
 *   访问周期 ≈ (ADDSET+1) + (DATAST+1) = 2 + 5 = 7 周期 ≈ 42 ns
 *
 * Bank3 (SRAM) 与 Bank4 (LCD) 独立时序，共享物理 D/A 总线，片选不同。
 */
static void ili9341_fsmc_init(void)
{
    FSMC_NORSRAM_TimingTypeDef rw = {0};

    __HAL_RCC_FSMC_CLK_ENABLE();

    hsram_lcd.Instance  = FSMC_NORSRAM_DEVICE;
    hsram_lcd.Extended  = FSMC_NORSRAM_EXTENDED_DEVICE;

    hsram_lcd.Init.NSBank             = FSMC_NORSRAM_BANK4;
    hsram_lcd.Init.DataAddressMux     = FSMC_DATA_ADDRESS_MUX_DISABLE;
    hsram_lcd.Init.MemoryType         = FSMC_MEMORY_TYPE_SRAM;
    hsram_lcd.Init.MemoryDataWidth    = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
    hsram_lcd.Init.BurstAccessMode    = FSMC_BURST_ACCESS_MODE_DISABLE;
    hsram_lcd.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    hsram_lcd.Init.WrapMode           = FSMC_WRAP_MODE_DISABLE;
    hsram_lcd.Init.WaitSignalActive   = FSMC_WAIT_TIMING_BEFORE_WS;
    hsram_lcd.Init.WriteOperation     = FSMC_WRITE_OPERATION_ENABLE;
    hsram_lcd.Init.WaitSignal         = FSMC_WAIT_SIGNAL_DISABLE;
    hsram_lcd.Init.ExtendedMode       = FSMC_EXTENDED_MODE_DISABLE;
    hsram_lcd.Init.AsynchronousWait   = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    hsram_lcd.Init.WriteBurst         = FSMC_WRITE_BURST_DISABLE;
    hsram_lcd.Init.PageSize           = FSMC_PAGE_SIZE_NONE;

    rw.AddressSetupTime      = 1;
    rw.AddressHoldTime       = 0;
    rw.DataSetupTime         = 4;
    rw.BusTurnAroundDuration = 0;
    rw.CLKDivision           = 0;
    rw.DataLatency           = 0;
    rw.AccessMode            = FSMC_ACCESS_MODE_A;

    if (HAL_SRAM_Init(&hsram_lcd, &rw, &rw) != HAL_OK)
    {
        rt_kprintf("ili9341: FSMC init failed\n");
    }
}

/* ---------- 公开绘图 API ---------- */

/*
 * 设置 GRAM 写入窗口并发送 RAMWR (0x2C)，后续连续写 ILI9341_RAM 即可刷像素。
 *
 * @param x0,y0  窗口左上角 (inclusive)
 * @param x1,y1  窗口右下角 (inclusive)
 *
 * 命令:
 *   0x2A CASET — 列地址 [x0, x1]
 *   0x2B PASET — 页(行)地址 [y0, y1]
 *   0x2C RAMWR — 开始内存写
 */
void ili9341_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    ili9341_write_reg(0x2A);
    ili9341_write_data(x0 >> 8);
    ili9341_write_data(x0 & 0xFF);
    ili9341_write_data(x1 >> 8);
    ili9341_write_data(x1 & 0xFF);

    ili9341_write_reg(0x2B);
    ili9341_write_data(y0 >> 8);
    ili9341_write_data(y0 & 0xFF);
    ili9341_write_data(y1 >> 8);
    ili9341_write_data(y1 & 0xFF);

    ili9341_write_reg(0x2C);
}

/*
 * 用单一颜色填充矩形区域。
 * @param color RGB565 像素值
 */
void ili9341_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    uint32_t total;

    ili9341_set_window(x0, y0, x1, y1);
    total = (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1);
    while (total--)
    {
        ILI9341_RAM = color;
    }
}

/* 清全屏为指定颜色 */
void ili9341_clear(uint16_t color)
{
    ili9341_fill_rect(0, 0, ILI9341_WIDTH - 1, ILI9341_HEIGHT - 1, color);
}

/*
 * 将 RGB565 像素缓冲刷到指定矩形 (LVGL flush_cb 调用)。
 *
 * @param colors  连续 RGB565 数组，长度 = (x1-x0+1)*(y1-y0+1)
 * 按行优先顺序写入 GRAM，与 LVGL 脏区缓冲布局一致。
 */
void ili9341_flush_area(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, const uint16_t *colors)
{
    uint32_t total;

    ili9341_set_window(x0, y0, x1, y1);
    total = (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1);
    while (total--)
    {
        ILI9341_RAM = *colors++;
    }
}

/* ---------- ILI9341 上电初始化序列 ---------- */

/*
 * 完整初始化: GPIO -> FSMC Bank4 -> 芯片寄存器配置 -> 开显示 -> 清屏 -> 背光。
 *
 * 寄存器序列参考 ILI9341 数据手册及启明欣欣例程，主要步骤:
 *   1. 软件复位 + 电源/VCOM/驱动时序配置
 *   2. MADCTL 设置竖屏方向 (BGR 位适配面板)
 *   3. COLMOD 0x55 = 16bpp (RGB565)
 *   4. Gamma 校正曲线
 *   5. 全屏地址范围 + Sleep Out + Display On
 *
 * @return RT_EOK
 */
int ili9341_init(void)
{
    ili9341_gpio_init();
    ili9341_fsmc_init();

    /* 上电后等待面板内部 LDO 稳定 */
    delay_ms(50);

    ili9341_write_reg(0x01);                         /* SWRESET: 软件复位 */
    delay_ms(120);

    /* ---- 电源与时序 ---- */
    ili9341_write_reg(0xCF);                         /* PWCTR B: Power Control B */
    ili9341_write_data(0x00);
    ili9341_write_data(0xC1);
    ili9341_write_data(0X30);

    ili9341_write_reg(0xED);                         /* PWCTR seq: Power on sequence */
    ili9341_write_data(0x64);
    ili9341_write_data(0x03);
    ili9341_write_data(0X12);
    ili9341_write_data(0X81);

    ili9341_write_reg(0xE8);                         /* TIMCTRL A: Driver timing A */
    ili9341_write_data(0x85);
    ili9341_write_data(0x10);
    ili9341_write_data(0x7A);

    ili9341_write_reg(0xCB);                         /* PWCTR A: Power Control A */
    ili9341_write_data(0x39);
    ili9341_write_data(0x2C);
    ili9341_write_data(0x00);
    ili9341_write_data(0x34);
    ili9341_write_data(0x02);

    ili9341_write_reg(0xF7);                         /* PUMP ratio: Pump ratio control */
    ili9341_write_data(0x20);

    ili9341_write_reg(0xEA);                         /* TIMCTRL B: Driver timing B */
    ili9341_write_data(0x00);
    ili9341_write_data(0x00);

    ili9341_write_reg(0xC0);                         /* PWCTR1: Power Control 1 (VRH) */
    ili9341_write_data(0x1B);
    ili9341_write_reg(0xC1);                         /* PWCTR2: Power Control 2 (BT) */
    ili9341_write_data(0x01);

    ili9341_write_reg(0xC5);                         /* VMCTR1: VCOM Control 1 */
    ili9341_write_data(0x30);
    ili9341_write_data(0x30);
    ili9341_write_reg(0xC7);                         /* VMCTR2: VCOM Control 2 */
    ili9341_write_data(0xB7);

    /* ---- 显示方向与像素格式 ---- */
    ili9341_write_reg(0x36);                         /* MADCTL: Memory Access Control */
    ili9341_write_data(ILI9341_MADCTL_PORTRAIT);     /* 竖屏 240x320, BGR 顺序 */

    ili9341_write_reg(0x3A);                         /* COLMOD: Pixel Format Set */
    ili9341_write_data(0x55);                        /* 0x55 = 16bit/pixel (RGB565) */

    /* ---- 帧率与显示功能 ---- */
    ili9341_write_reg(0xB1);                         /* FRMCTR1: Frame Rate Control */
    ili9341_write_data(0x00);
    ili9341_write_data(0x1A);

    ili9341_write_reg(0xB6);                         /* DFUNCTR: Display Function Control */
    ili9341_write_data(0x0A);
    ili9341_write_data(0xA2);

    /* ---- Gamma 校正 ---- */
    ili9341_write_reg(0xF2);                         /* GAMMAFN: 3Gamma Function Disable */
    ili9341_write_data(0x00);                        /* 0 = 启用 Gamma 曲线 */

    ili9341_write_reg(0x26);                         /* GAMSET: Gamma curve selected */
    ili9341_write_data(0x01);                        /* Gamma curve 1 */

    ili9341_write_reg(0xE0);                         /* GMCTRP1: Positive Gamma Correction */
    ili9341_write_data(0x0F);
    ili9341_write_data(0x2A);
    ili9341_write_data(0x28);
    ili9341_write_data(0x08);
    ili9341_write_data(0x0E);
    ili9341_write_data(0x08);
    ili9341_write_data(0x54);
    ili9341_write_data(0xA9);
    ili9341_write_data(0x43);
    ili9341_write_data(0x0A);
    ili9341_write_data(0x0F);
    ili9341_write_data(0x00);
    ili9341_write_data(0x00);
    ili9341_write_data(0x00);
    ili9341_write_data(0x00);

    ili9341_write_reg(0xE1);                         /* GMCTRN1: Negative Gamma Correction */
    ili9341_write_data(0x00);
    ili9341_write_data(0x15);
    ili9341_write_data(0x17);
    ili9341_write_data(0x07);
    ili9341_write_data(0x11);
    ili9341_write_data(0x06);
    ili9341_write_data(0x2B);
    ili9341_write_data(0x56);
    ili9341_write_data(0x3C);
    ili9341_write_data(0x05);
    ili9341_write_data(0x10);
    ili9341_write_data(0x0F);
    ili9341_write_data(0x3F);
    ili9341_write_data(0x3F);
    ili9341_write_data(0x0F);

    /* ---- 设置全屏 GRAM 地址范围 (竖屏: 列 0-239, 行 0-319) ---- */
    ili9341_write_reg(0x2B);                         /* PASET: 行 0 ~ 319 (0x013F) */
    ili9341_write_data(0x00);
    ili9341_write_data(0x00);
    ili9341_write_data(0x01);
    ili9341_write_data(0x3F);
    ili9341_write_reg(0x2A);                         /* CASET: 列 0 ~ 239 (0x00EF) */
    ili9341_write_data(0x00);
    ili9341_write_data(0x00);
    ili9341_write_data(0x00);
    ili9341_write_data(0xEF);

    /* ---- 退出休眠并开启显示 ---- */
    ili9341_write_reg(0x11);                         /* SLPOUT: Sleep Out */
    delay_ms(120);                                   /* 手册要求 Sleep Out 后等待 120ms */
    ili9341_write_reg(0x29);                         /* DISPON: Display ON */

    ili9341_clear(0x0000);                           /* 清屏为黑色 */
    ILI9341_BL_ON();                                 /* 打开背光 */

    rt_kprintf("ili9341: ready (%dx%d, 16bpp)\n", ILI9341_WIDTH, ILI9341_HEIGHT);
    return RT_EOK;
}
