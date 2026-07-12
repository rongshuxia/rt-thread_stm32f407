/*
 * CPU-only buffers in CCM @ 0x10000000 (fixed address).
 */

#include "ccmram_bufs.h"

#if defined(__CC_ARM) || defined(__CLANG_ARM)
ccmram_bufs_t ccmram_bufs __attribute__((at(0x10000000))) = { 0 };
#else
ccmram_bufs_t ccmram_bufs = { 0 };
#endif
