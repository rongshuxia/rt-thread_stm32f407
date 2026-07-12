/*
 * IS62WV51216 - 16-bit async SRAM on FSMC Bank1 NE3 (PG10), 0x68000000.
 *
 * 芯片: ISSI IS62WV51216 (512K x 16 bit = 1 MB, 异步 SRAM, -55ns 速度等级)
 * 板卡: 启明欣欣 STM32F407ZG 开发板(高配版)
 *
 * FSMC 总线拓扑 (与 ILI9341 LCD 共享数据线/地址线):
 *   Bank1 NE3 / PG10  -> SRAM 片选 CE#
 *   Bank1 NE4 / PG12  -> LCD  片选 CE#  (本驱动不操作，但 GPIO 需一并复用)
 *   NBL0/PE0, NBL1/PE1 -> 16 位字节选通 (低/高字节)
 *   NOE/PD4, NWE/PD5   -> 读/写使能
 *   D0-D15             -> 16 位数据总线
 *   A0-A18             -> 19 位地址 (512K word 寻址)
 *
 * 内存映射 (见 is62wv51216.h):
 *   0x68000000          LVGL 堆池 (768 KB)
 *   0x680C0000          LVGL 绘制缓冲 (240x32 RGB565, ~15 KB)
 *   剩余空间            可用于其他大块缓冲 (如 JPEG 等)
 *
 * 启动: INIT_BOARD_EXPORT 在 board 阶段自动调用 is62wv51216_init()。
 */

#include "is62wv51216.h"
#include <board.h>
#include <stm32f4xx_hal.h>

/* HAL SRAM 句柄，供 HAL_SRAM_Init 及后续扩展 (DMA 等) 使用 */
static SRAM_HandleTypeDef hsram_ext;

/* GPIO 与 FSMC 控制器分别只初始化一次，避免重复配置 */
static rt_bool_t fsmc_gpio_ready = RT_FALSE;
static rt_bool_t sram_ready = RT_FALSE;

/*
 * 配置 FSMC 复用 GPIO。
 *
 * 所有引脚: 复用推挽 AF12_FSMC, 上拉, 最高速度。
 * 与 LCD 共用 D/A 总线，因此即使只访问 SRAM，也必须初始化完整总线引脚。
 */
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

    /*
     * GPIOD: 数据 D0-D3, D13-D15 + 控制线
     *   PD0  FSMC_D2        PD1  FSMC_D3
     *   PD4  FSMC_NOE (读)  PD5  FSMC_NWE (写)
     *   PD8  FSMC_D13       PD9  FSMC_D14      PD10 FSMC_D15
     *   PD11 FSMC_A16       PD12 FSMC_A17      PD13 FSMC_A18
     *   PD14 FSMC_D0        PD15 FSMC_D1
     */
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5
          |  GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10
          |  GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13
          |  GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gi);

    /*
     * GPIOE: 字节选通 + 数据 D4-D12
     *   PE0  FSMC_NBL0 (低字节)   PE1  FSMC_NBL1 (高字节)
     *   PE7  FSMC_D4  .. PE15 FSMC_D12
     */
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1
          |  GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10
          |  GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gi);

    /*
     * GPIOF: 地址 A0-A9
     *   PF0-PF5  FSMC_A0-A5
     *   PF12-PF15 FSMC_A6-A9
     */
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3
          |  GPIO_PIN_4 | GPIO_PIN_5
          |  GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &gi);

    /*
     * GPIOG: 地址 A10-A15 + 片选
     *   PG0-PG5   FSMC_A10-A15
     *   PG10      FSMC_NE3 -> SRAM CE#
     *   PG12      FSMC_NE4 -> LCD CE#
     */
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3
          |  GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_10 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOG, &gi);

    fsmc_gpio_ready = RT_TRUE;
}

/*
 * HAL 弱回调: SRAM 外设初始化前由 HAL_SRAM_Init 调用。
 * 负责开启 FSMC 时钟并完成 GPIO 复用。
 */
void HAL_SRAM_MspInit(SRAM_HandleTypeDef *hsram)
{
    (void)hsram;
    __HAL_RCC_FSMC_CLK_ENABLE();
    fsmc_gpio_init();
}

/* HAL 弱回调: 关闭 FSMC 时钟 (当前工程未主动调用 DeInit) */
void HAL_SRAM_MspDeInit(SRAM_HandleTypeDef *hsram)
{
    (void)hsram;
    __HAL_RCC_FSMC_CLK_DISABLE();
}

/*
 * 配置 FSMC Bank1 子 Bank3 为 16 位异步 SRAM 模式。
 *
 * 关键参数说明:
 *   NSBank = BANK3       -> 对应 NE3/PG10，映射基址 0x68000000
 *   MemoryType = SRAM    -> 异步 SRAM (非 NOR Flash)
 *   MemoryDataWidth = 16 -> 与芯片 16 位数据宽度一致
 *   ExtendedMode = DISABLE -> 读写使用同一套时序 (rw 传两次)
 *   WriteOperation = ENABLE -> 允许写访问
 *
 * 时序 (Access Mode A, HCLK = 168 MHz, 周期 ≈ 6 ns):
 *   IS62WV51216-55 要求 tRC/tWC >= 55 ns
 *   读/写周期 ≈ (ADDSET+1) + (DATAST+1) = (1+1) + (8+1) = 11 周期 ≈ 66 ns > 55 ns
 *
 *   AddressSetupTime = 1  -> 地址建立 2 个 HCLK
 *   DataSetupTime    = 8  -> 数据阶段 9 个 HCLK
 */
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

/*
 * 初始化外扩 SRAM。
 * 可重复调用，内部 sram_ready 保证只执行一次 FSMC 配置。
 *
 * @return RT_EOK 成功; HAL 失败时仍返回 RT_EOK 但会打印错误 (可按需加强)
 */
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

/*
 * 按 16 位字写入外扩 SRAM。
 *
 * @param byte_offset  字节偏移 (相对 0x68000000)，须 2 字节对齐
 * @param data         要写入的 16 位数据
 *
 * 通过 volatile 指针直接映射访问，无需额外驱动层拷贝。
 */
void is62wv51216_write16(uint32_t byte_offset, uint16_t data)
{
    volatile uint16_t *p = IS62WV51216_PTR + (byte_offset / 2U);
    *p = data;
}

/*
 * 按 16 位字读取外扩 SRAM。
 *
 * @param byte_offset  字节偏移，须 2 字节对齐
 * @return 读到的 16 位数据
 */
uint16_t is62wv51216_read16(uint32_t byte_offset)
{
    volatile uint16_t *p = IS62WV51216_PTR + (byte_offset / 2U);
    return *p;
}

/*
 * 批量写入 (按 16 位字拷贝)。
 *
 * @param byte_offset  起始字节偏移，须 2 字节对齐
 * @param buf          源缓冲区 (按 uint16_t 解释)
 * @param len          字节长度，须为 2 的倍数
 *
 * 未做边界检查，调用方需保证 offset+len 不超出 IS62WV51216_SIZE。
 */
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

/*
 * 批量读取 (按 16 位字拷贝)。
 *
 * @param byte_offset  起始字节偏移，须 2 字节对齐
 * @param buf          目标缓冲区
 * @param len          字节长度，须为 2 的倍数
 */
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

/*
 * 简易自检: 在首、中、末三个位置写入固定模式并回读比对。
 * 可用于 MSH 命令或启动阶段验证 FSMC 接线与时序。
 *
 * @return RT_EOK 全部通过; -RT_ERROR 任一地址读写不一致
 */
int is62wv51216_test(void)
{
    static const struct
    {
        uint32_t off;
        uint16_t val;
    } pts[] =
    {
        { 0x00000U, 0xA5A5U },                      /* 起始地址 */
        { 0x80000U, 0x5AA5U },                      /* 512 KB 处 (中间) */
        { IS62WV51216_SIZE - 2U, 0x1234U },         /* 最后一个 16 位字 */
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

/* RT-Thread 板级自动初始化: 优先级 "board level" */
INIT_BOARD_EXPORT(is62wv51216_init);
