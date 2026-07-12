/*
 * IS62WV51216 external SRAM driver (512K x 16, 1 MB)
 *
 * Board: 启明欣欣 STM32F407ZG (FSMC Bank1 sub-bank 3, NE3 / PG10)
 *   Base address: 0x68000000
 *   LCD (ILI9341) shares FSMC data/address bus on Bank4 (NE4 / PG12).
 */

#ifndef __IS62WV51216_H__
#define __IS62WV51216_H__

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IS62WV51216_BASE_ADDR   ((uint32_t)0x68000000U)
#define IS62WV51216_SIZE        (1024U * 1024U)
#define IS62WV51216_END_ADDR    (IS62WV51216_BASE_ADDR + IS62WV51216_SIZE - 1U)

#define IS62WV51216_PTR         ((volatile uint16_t *)IS62WV51216_BASE_ADDR)

/* LVGL dedicated pool at the start of external SRAM */
#define IS62WV51216_LVGL_MEM_ADDR   IS62WV51216_BASE_ADDR
#define IS62WV51216_LVGL_MEM_SIZE     (768U * 1024U)
/* Draw buffer follows the LVGL pool (~15 KB for 240x32 RGB565) */
#define IS62WV51216_DRAW_BUF_ADDR     (IS62WV51216_LVGL_MEM_ADDR + IS62WV51216_LVGL_MEM_SIZE)
#define IS62WV51216_DRAW_BUF_SIZE     (240U * 32U * 2U)

int  is62wv51216_init(void);
int  is62wv51216_test(void);

void     is62wv51216_write16(uint32_t byte_offset, uint16_t data);
uint16_t is62wv51216_read16(uint32_t byte_offset);
void     is62wv51216_write_buffer(uint32_t byte_offset, const void *buf, uint32_t len);
void     is62wv51216_read_buffer(uint32_t byte_offset, void *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* __IS62WV51216_H__ */
