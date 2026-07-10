/*
 * ILI9341 TFT-LCD driver (16-bit FSMC parallel)
 *
 * Wiring (STM32F407ZG, FSMC Bank1 sub-bank 4, NE4 + A12 as RS):
 *   FSMC_NE4 - PG12       FSMC_NWE - PD5        FSMC_NOE - PD4
 *   FSMC_A12 - PG2 (RS)
 *   FSMC_D0..D15 - see board IO map
 *   LCD_BL   - PF10 (high active)
 *   T_PEN    - PF11 (touch IRQ, unused here)
 */

#ifndef __ILI9341_H__
#define __ILI9341_H__

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default orientation: portrait 240x320 */
#define ILI9341_WIDTH    240
#define ILI9341_HEIGHT   320

/* MADCTL (0x36) orientation, BGR=0x08 always set for this panel */
#define ILI9341_MADCTL_PORTRAIT      0x08  /* BGR only, no mirror       */
#define ILI9341_MADCTL_PORTRAIT_180  0xC8  /* MY | MX | BGR             */
#define ILI9341_MADCTL_LANDSCAPE     0x28  /* MV | BGR, rotation 90     */
#define ILI9341_MADCTL_LANDSCAPE_270 0xE8  /* MY | MX | MV | BGR        */

/* Backlight */
#define ILI9341_BL_ON()    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_10, GPIO_PIN_SET)
#define ILI9341_BL_OFF()   HAL_GPIO_WritePin(GPIOF, GPIO_PIN_10, GPIO_PIN_RESET)

/* FSMC-mapped registers.
 * NE4 base = 0x6C000000. With 16-bit data bus FSMC shifts HADDR by 1, so
 * driving FSMC_A12 high needs HADDR bit 13 set -> data window offset 0x2000.
 */
#define ILI9341_BASE       ((uint32_t)0x6C000000)
#define ILI9341_REG_ADDR   (ILI9341_BASE)                    /* RS=0 : command */
#define ILI9341_RAM_ADDR   (ILI9341_BASE | (1u << 13))       /* RS=1 : data    */

#define ILI9341_REG    (*((volatile uint16_t *)ILI9341_REG_ADDR))
#define ILI9341_RAM    (*((volatile uint16_t *)ILI9341_RAM_ADDR))

int  ili9341_init(void);
void ili9341_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void ili9341_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
void ili9341_flush_area(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, const uint16_t *colors);
void ili9341_clear(uint16_t color);

#ifdef __cplusplus
}
#endif

#endif /* __ILI9341_H__ */
