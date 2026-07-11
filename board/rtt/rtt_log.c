/*
 * J-Link RTT log output (independent from rt_kprintf / UART console).
 */

#include "rtt_log.h"
#include "SEGGER_RTT.h"

#include <stdarg.h>

#define RTT_LOG_BUF_SIZE    256
#define RTT_LOG_CHANNEL     0

static int rtt_log_board_init(void)
{
    SEGGER_RTT_Init();
    return 0;
}
INIT_BOARD_EXPORT(rtt_log_board_init);

void rtt_log_write(const char *buf, rt_size_t len)
{
    rt_base_t level;

    if ((buf == RT_NULL) || (len == 0))
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    SEGGER_RTT_WriteNoLock(RTT_LOG_CHANNEL, buf, (unsigned)len);
    rt_hw_interrupt_enable(level);
}

void rtt_log_printf(const char *fmt, ...)
{
    char buf[RTT_LOG_BUF_SIZE];
    va_list args;
    rt_size_t len;

    va_start(args, fmt);
    len = rt_vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);

    if (len > sizeof(buf) - 1)
    {
        len = sizeof(buf) - 1;
    }

    rtt_log_write(buf, len);
}
