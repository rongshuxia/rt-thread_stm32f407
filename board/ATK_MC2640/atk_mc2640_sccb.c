/*
 * ATK-MC2640 SCCB driver (OV2640 register bus, I2C-like timing).
 *
 * Pins: PD6 = SCL, PD7 = SDA, software bit-bang (no HW I2C).
 * When ATK_MC2640_SCCB_GPIO_PULLUP=1, SDA is switched to input only while
 * reading a byte; otherwise SDA stays open-drain with pull-up.
 *
 * Typical OV2640 transactions:
 *   3-phase write: START + ID(W) + reg + data + STOP
 *   2-phase write: START + ID(W) + reg + STOP       (set read pointer)
 *   2-phase read:  START + ID(R) + data + STOP
 */

#include "atk_mc2640_sccb.h"
#include "stm32f4xx_hal.h"
#include "stm32f4_delay.h"

/* LSB of SCCB byte address: 0 = write, 1 = read */
#define ATK_MC2640_SCCB_WRITE   0x00
#define ATK_MC2640_SCCB_READ    0x01

#if (ATK_MC2640_SCCB_GPIO_PULLUP != 0)
/* Toggle SDA between push-pull output and input for bit read */
static void atk_mc2640_sccb_set_sda_output(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};

    gpio_init_struct.Pin    = ATK_MC2640_SCCB_SDA_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_SCCB_SDA_GPIO_PORT, &gpio_init_struct);
}

static void atk_mc2640_sccb_set_sda_input(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};

    gpio_init_struct.Pin    = ATK_MC2640_SCCB_SDA_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_INPUT;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_SCCB_SDA_GPIO_PORT, &gpio_init_struct);
}
#endif

static inline void atk_mc2640_sccb_delay(void)
{
    stm32f4_delay_us(5);
}

static void atk_mc2640_sccb_start(void)
{
    ATK_MC2640_SCCB_SDA(1);
    ATK_MC2640_SCCB_SCL(1);
    atk_mc2640_sccb_delay();
    ATK_MC2640_SCCB_SDA(0);
    atk_mc2640_sccb_delay();
    ATK_MC2640_SCCB_SCL(0);
}

static void atk_mc2640_sccb_stop(void)
{
    ATK_MC2640_SCCB_SDA(0);
    atk_mc2640_sccb_delay();
    ATK_MC2640_SCCB_SCL(1);
    atk_mc2640_sccb_delay();
    ATK_MC2640_SCCB_SDA(1);
    atk_mc2640_sccb_delay();
}

/* Shift out 8 bits, then clock the ACK/NACK slot with SDA released */
static void atk_mc2640_sccb_write_byte(uint8_t dat)
{
    int8_t dat_index;
    uint8_t dat_bit;

    for (dat_index = 7; dat_index >= 0; dat_index--)
    {
        dat_bit = (dat >> dat_index) & 0x01;
        ATK_MC2640_SCCB_SDA(dat_bit);
        atk_mc2640_sccb_delay();
        ATK_MC2640_SCCB_SCL(1);
        atk_mc2640_sccb_delay();
        ATK_MC2640_SCCB_SCL(0);
    }

    ATK_MC2640_SCCB_SDA(1);
    atk_mc2640_sccb_delay();
    ATK_MC2640_SCCB_SCL(1);
    atk_mc2640_sccb_delay();
    ATK_MC2640_SCCB_SCL(0);
}

/* Shift in 8 bits; drive SDA low after last bit (NACK) */
static void atk_mc2640_sccb_read_byte(uint8_t *dat)
{
    int8_t dat_index;
    uint8_t dat_bit;

#if (ATK_MC2640_SCCB_GPIO_PULLUP != 0)
    atk_mc2640_sccb_set_sda_input();
#endif

    *dat = 0;
    for (dat_index = 7; dat_index >= 0; dat_index--)
    {
        atk_mc2640_sccb_delay();
        ATK_MC2640_SCCB_SCL(1);
        dat_bit = ATK_MC2640_SCCB_READ_SDA();
        *dat |= (dat_bit << dat_index);
        atk_mc2640_sccb_delay();
        ATK_MC2640_SCCB_SCL(0);
    }

    atk_mc2640_sccb_delay();
    ATK_MC2640_SCCB_SCL(1);
    atk_mc2640_sccb_delay();
    ATK_MC2640_SCCB_SCL(0);
    atk_mc2640_sccb_delay();
    ATK_MC2640_SCCB_SDA(0);
    atk_mc2640_sccb_delay();

#if (ATK_MC2640_SCCB_GPIO_PULLUP != 0)
    atk_mc2640_sccb_set_sda_output();
#endif
}

void atk_mc2640_sccb_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};

    ATK_MC2640_SCCB_SCL_GPIO_CLK_ENABLE();
    ATK_MC2640_SCCB_SDA_GPIO_CLK_ENABLE();

    gpio_init_struct.Pin    = ATK_MC2640_SCCB_SCL_GPIO_PIN;
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_SCCB_SCL_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin    = ATK_MC2640_SCCB_SDA_GPIO_PIN;
#if (ATK_MC2640_SCCB_GPIO_PULLUP != 0)
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_PP;
#else
    gpio_init_struct.Mode   = GPIO_MODE_OUTPUT_OD;
#endif
    gpio_init_struct.Pull   = GPIO_PULLUP;
    gpio_init_struct.Speed  = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_MC2640_SCCB_SDA_GPIO_PORT, &gpio_init_struct);

    atk_mc2640_sccb_stop();
}

void atk_mc2640_sccb_3_phase_write(uint8_t id_addr, uint8_t sub_addr, uint8_t dat)
{
    atk_mc2640_sccb_start();
    atk_mc2640_sccb_write_byte((id_addr << 1) | ATK_MC2640_SCCB_WRITE);
    atk_mc2640_sccb_write_byte(sub_addr);
    atk_mc2640_sccb_write_byte(dat);
    atk_mc2640_sccb_stop();
}

void atk_mc2640_sccb_2_phase_write(uint8_t id_addr, uint8_t sub_addr)
{
    atk_mc2640_sccb_start();
    atk_mc2640_sccb_write_byte((id_addr << 1) | ATK_MC2640_SCCB_WRITE);
    atk_mc2640_sccb_write_byte(sub_addr);
    atk_mc2640_sccb_stop();
}

void atk_mc2640_sccb_2_phase_read(uint8_t id_addr, uint8_t *dat)
{
    atk_mc2640_sccb_start();
    atk_mc2640_sccb_write_byte((id_addr << 1) | ATK_MC2640_SCCB_READ);
    atk_mc2640_sccb_read_byte(dat);
    atk_mc2640_sccb_stop();
}
