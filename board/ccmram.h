/*
 * STM32F407 CCM (Core Coupled Memory) helpers.
 *
 * CCM: 64 KB @ 0x10000000, CPU-only (no DMA). Place large CPU buffers here
 * via CCMRAM_SECTION to free main SRAM (0x20000000).
 */

#ifndef __CCMRAM_H__
#define __CCMRAM_H__

#include <rtconfig.h>
#include "stm32f4xx.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__CC_ARM) || defined(__CLANG_ARM)
#define CCMRAM_SECTION  __attribute__((section(".CCMRAM"), aligned(4)))
#elif defined(__GNUC__)
#define CCMRAM_SECTION  __attribute__((section(".CCMRAM"), aligned(4)))
#else
#define CCMRAM_SECTION
#endif

static inline void ccmram_init(void)
{
#ifdef RCC_AHB1ENR_CCMDATARAMEN
    __HAL_RCC_CCMDATARAMEN_CLK_ENABLE();
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* __CCMRAM_H__ */
