/*
 * ILI9341 driver - 16-bit FSMC parallel, portrait 240x320.
 */

#include "ili9341.h"
#include <board.h>
#include <stm32f4xx_hal.h>
#include "stm32f4_delay.h"

extern void rt_hw_us_delay(rt_uint32_t us);

/* SysTick-polling delay - safe both before and after RT-Thread scheduler
 * starts. (rt_thread_mdelay would only work post-scheduler; HAL_Delay is
 * stubbed out in this BSP.) */
static void delay_ms(uint32_t ms)
{
	rt_thread_mdelay(ms);
	
//    while (ms--)
//    {
//        rt_hw_us_delay(1000);
//    }
}

static SRAM_HandleTypeDef hsram_lcd;

/* ---------- low level helpers ---------- */

static inline void ili9341_write_reg(uint16_t reg)
{
    ILI9341_REG = reg;
}

static inline void ili9341_write_data(uint16_t data)
{
    ILI9341_RAM = data;
}

static inline void ili9341_write_cmd_data(uint16_t reg, uint16_t data)
{
    ILI9341_REG = reg;
    ILI9341_RAM = data;
}

/* ---------- pin / FSMC bring-up ---------- */

static void ili9341_gpio_init(void)
{
    GPIO_InitTypeDef gi = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* PD0/1/4/5/8/9/10/14/15 -> FSMC */
    gi.Mode      = GPIO_MODE_AF_PP;
    gi.Pull      = GPIO_PULLUP;
    gi.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gi.Alternate = GPIO_AF12_FSMC;
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5
          |  GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10
          |  GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gi);

    /* PE7..PE15 -> FSMC */
    gi.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10
          |  GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gi);

    /* PG2 (A12), PG12 (NE4) -> FSMC */
    gi.Pin = GPIO_PIN_2 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOG, &gi);

    /* PF10 backlight push-pull */
    gi.Mode      = GPIO_MODE_OUTPUT_PP;
    gi.Pull      = GPIO_NOPULL;
    gi.Speed     = GPIO_SPEED_FREQ_LOW;
    gi.Alternate = 0;
    gi.Pin       = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOF, &gi);
    ILI9341_BL_OFF();

    /* PF11 touch IRQ input (just configured, not used yet) */
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pull = GPIO_PULLUP;
    gi.Pin  = GPIO_PIN_11;
    HAL_GPIO_Init(GPIOF, &gi);
}

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

    /* Mode A asynchronous SRAM, AHB 168MHz -> period ~6ns
     * Address setup 1 + data 4 ~ tight but reliable for ILI9341 */
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

/* ---------- public API ---------- */

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

void ili9341_clear(uint16_t color)
{
    ili9341_fill_rect(0, 0, ILI9341_WIDTH - 1, ILI9341_HEIGHT - 1, color);
}

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

/* ---------- ILI9341 startup sequence ---------- */

int ili9341_init(void)
{
    ili9341_gpio_init();
    ili9341_fsmc_init();

    /* The panel takes a while after power-up before commands are accepted. */
    delay_ms(50);

    ili9341_write_reg(0x01);                         /* Software reset */
    delay_ms(120);

    ili9341_write_reg(0xCF);                         /* Power Control B */
    ili9341_write_data(0x00);
    ili9341_write_data(0xC1);
    ili9341_write_data(0X30);

    ili9341_write_reg(0xED);                         /* Power on sequence */
    ili9341_write_data(0x64);
    ili9341_write_data(0x03);
    ili9341_write_data(0X12);
    ili9341_write_data(0X81);

    ili9341_write_reg(0xE8);                         /* Driver timing A */
    ili9341_write_data(0x85);
    ili9341_write_data(0x10);
    ili9341_write_data(0x7A);

    ili9341_write_reg(0xCB);                         /* Power Control A */
    ili9341_write_data(0x39);
    ili9341_write_data(0x2C);
    ili9341_write_data(0x00);
    ili9341_write_data(0x34);
    ili9341_write_data(0x02);

    ili9341_write_reg(0xF7);                         /* Pump ratio */
    ili9341_write_data(0x20);

    ili9341_write_reg(0xEA);                         /* Driver timing B */
    ili9341_write_data(0x00);
    ili9341_write_data(0x00);

    ili9341_write_reg(0xC0);                         /* Power Control 1 */
    ili9341_write_data(0x1B);
    ili9341_write_reg(0xC1);                         /* Power Control 2 */
    ili9341_write_data(0x01);

    ili9341_write_reg(0xC5);                         /* VCOM 1 */
    ili9341_write_data(0x30);
    ili9341_write_data(0x30);
    ili9341_write_reg(0xC7);                         /* VCOM 2 */
    ili9341_write_data(0xB7);

    ili9341_write_reg(0x36);
    ili9341_write_data(ILI9341_MADCTL_PORTRAIT);

    ili9341_write_reg(0x3A);                         /* 16-bit pixel format */
    ili9341_write_data(0x55);

    ili9341_write_reg(0xB1);                         /* Frame rate */
    ili9341_write_data(0x00);
    ili9341_write_data(0x1A);

    ili9341_write_reg(0xB6);                         /* Display function */
    ili9341_write_data(0x0A);
    ili9341_write_data(0xA2);

    ili9341_write_reg(0xF2);                         /* 3-gamma disable */
    ili9341_write_data(0x00);

    ili9341_write_reg(0x26);                         /* Gamma curve 1 */
    ili9341_write_data(0x01);

    ili9341_write_reg(0xE0);                         /* Positive gamma */
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

    ili9341_write_reg(0xE1);                         /* Negative gamma */
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

    ili9341_write_reg(0x2B);
    ili9341_write_data(0x00);
    ili9341_write_data(0x00);
    ili9341_write_data(0x01);
    ili9341_write_data(0x3F);
    ili9341_write_reg(0x2A);
    ili9341_write_data(0x00);
    ili9341_write_data(0x00);
    ili9341_write_data(0x00);
    ili9341_write_data(0xEF);

    ili9341_write_reg(0x11);                         /* Sleep out */
    delay_ms(120);
    ili9341_write_reg(0x29);                         /* Display ON */

    ili9341_clear(0x0000);
    ILI9341_BL_ON();

    rt_kprintf("ili9341: ready (%dx%d, 16bpp)\n", ILI9341_WIDTH, ILI9341_HEIGHT);
    return RT_EOK;
}
