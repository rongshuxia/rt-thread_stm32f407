/*
 * IS62WV51216 - 16-bit async SRAM on FSMC Bank1 NE3 (PG10), 0x68000000.
 *
 * Pin map matches 启明欣欣407开发板(高配版) FSMC wiring (shared with ILI9341):
 *   NE3/PG10 -> SRAM CE#     NE4/PG12 -> LCD CE#
 *   NBL0/PE0, NBL1/PE1       NOE/PD4, NWE/PD5
 *   D0-D15, A0-A18           (see fsmc_gpio_init)
 */

#include "is62wv51216.h"
#include <board.h>
#include <stm32f4xx_hal.h>

static SRAM_HandleTypeDef hsram_ext;
static rt_bool_t fsmc_gpio_ready = RT_FALSE;
static rt_bool_t sram_ready = RT_FALSE;

static void fsmc_gpio_init(void)
{
    GPIO_InitTypeDef gi = {0};

    if (fsmc_gpio_ready)
    {
        return;
    }

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    gi.Mode      = GPIO_MODE_AF_PP;
    gi.Pull      = GPIO_PULLUP;
    gi.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gi.Alternate = GPIO_AF12_FSMC;

    /* PD0/1/4/5/8/9/10/11/12/13/14/15 */
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5
          |  GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10
          |  GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13
          |  GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gi);

    /* PE0/1 (NBL), PE7..PE15 (D4-D12) */
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1
          |  GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10
          |  GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gi);

    /* PF0..PF5, PF12..PF15 (A0-A9) */
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3
          |  GPIO_PIN_4 | GPIO_PIN_5
          |  GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &gi);

    /* PG0..PG5 (A10-A15), PG10 (NE3), PG12 (NE4/LCD) */
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3
          |  GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_10 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOG, &gi);

    fsmc_gpio_ready = RT_TRUE;
}

void HAL_SRAM_MspInit(SRAM_HandleTypeDef *hsram)
{
    (void)hsram;
    __HAL_RCC_FSMC_CLK_ENABLE();
    fsmc_gpio_init();
}

void HAL_SRAM_MspDeInit(SRAM_HandleTypeDef *hsram)
{
    (void)hsram;
    __HAL_RCC_FSMC_CLK_DISABLE();
}

static void is62wv51216_fsmc_init(void)
{
    FSMC_NORSRAM_TimingTypeDef rw = {0};

    hsram_ext.Instance  = FSMC_NORSRAM_DEVICE;
    hsram_ext.Extended  = FSMC_NORSRAM_EXTENDED_DEVICE;

    hsram_ext.Init.NSBank             = FSMC_NORSRAM_BANK3;
    hsram_ext.Init.DataAddressMux     = FSMC_DATA_ADDRESS_MUX_DISABLE;
    hsram_ext.Init.MemoryType         = FSMC_MEMORY_TYPE_SRAM;
    hsram_ext.Init.MemoryDataWidth    = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
    hsram_ext.Init.BurstAccessMode    = FSMC_BURST_ACCESS_MODE_DISABLE;
    hsram_ext.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    hsram_ext.Init.WrapMode           = FSMC_WRAP_MODE_DISABLE;
    hsram_ext.Init.WaitSignalActive   = FSMC_WAIT_TIMING_BEFORE_WS;
    hsram_ext.Init.WriteOperation     = FSMC_WRITE_OPERATION_ENABLE;
    hsram_ext.Init.WaitSignal         = FSMC_WAIT_SIGNAL_DISABLE;
    hsram_ext.Init.ExtendedMode       = FSMC_EXTENDED_MODE_DISABLE;
    hsram_ext.Init.AsynchronousWait   = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    hsram_ext.Init.WriteBurst         = FSMC_WRITE_BURST_DISABLE;
    hsram_ext.Init.PageSize           = FSMC_PAGE_SIZE_NONE;

    /* IS62WV51216-55: tWC/tRC >= 55ns; at 168MHz HCLK ~6ns per tick */
    rw.AddressSetupTime      = 1;
    rw.AddressHoldTime       = 0;
    rw.DataSetupTime         = 8;
    rw.BusTurnAroundDuration = 0;
    rw.CLKDivision           = 0;
    rw.DataLatency           = 0;
    rw.AccessMode            = FSMC_ACCESS_MODE_A;

    if (HAL_SRAM_Init(&hsram_ext, &rw, &rw) != HAL_OK)
    {
        rt_kprintf("is62wv51216: FSMC bank3 init failed\n");
    }
}

int is62wv51216_init(void)
{
    if (sram_ready)
    {
        return RT_EOK;
    }

    is62wv51216_fsmc_init();
    sram_ready = RT_TRUE;
    rt_kprintf("is62wv51216: ready (base=0x%08X, size=%u KB)\n",
               IS62WV51216_BASE_ADDR, IS62WV51216_SIZE / 1024U);
    return RT_EOK;
}

void is62wv51216_write16(uint32_t byte_offset, uint16_t data)
{
    volatile uint16_t *p = IS62WV51216_PTR + (byte_offset / 2U);
    *p = data;
}

uint16_t is62wv51216_read16(uint32_t byte_offset)
{
    volatile uint16_t *p = IS62WV51216_PTR + (byte_offset / 2U);
    return *p;
}

void is62wv51216_write_buffer(uint32_t byte_offset, const void *buf, uint32_t len)
{
    const uint16_t *src = (const uint16_t *)buf;
    volatile uint16_t *dst = IS62WV51216_PTR + (byte_offset / 2U);
    uint32_t words = len / 2U;

    while (words--)
    {
        *dst++ = *src++;
    }
}

void is62wv51216_read_buffer(uint32_t byte_offset, void *buf, uint32_t len)
{
    uint16_t *dst = (uint16_t *)buf;
    volatile uint16_t *src = IS62WV51216_PTR + (byte_offset / 2U);
    uint32_t words = len / 2U;

    while (words--)
    {
        *dst++ = *src++;
    }
}

int is62wv51216_test(void)
{
    static const struct
    {
        uint32_t off;
        uint16_t val;
    } pts[] =
    {
        { 0x00000U, 0xA5A5U },
        { 0x80000U, 0x5AA5U },
        { IS62WV51216_SIZE - 2U, 0x1234U },
    };
    uint32_t i;

    for (i = 0; i < sizeof(pts) / sizeof(pts[0]); i++)
    {
        uint16_t rb;

        is62wv51216_write16(pts[i].off, pts[i].val);
        rb = is62wv51216_read16(pts[i].off);
        if (rb != pts[i].val)
        {
            rt_kprintf("is62wv51216: test fail @0x%06X wrote 0x%04X read 0x%04X\n",
                       pts[i].off, pts[i].val, rb);
            return -RT_ERROR;
        }
    }

    rt_kprintf("is62wv51216: self-test OK\n");
    return RT_EOK;
}

INIT_BOARD_EXPORT(is62wv51216_init);
