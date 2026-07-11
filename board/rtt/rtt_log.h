#ifndef RTT_LOG_H__
#define RTT_LOG_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

void rtt_log_write(const char *buf, rt_size_t len);
void rtt_log_printf(const char *fmt, ...);

#define RTT_LOG_I(fmt, ...)  rtt_log_printf("[I] " fmt "\n", ##__VA_ARGS__)
#define RTT_LOG_W(fmt, ...)  rtt_log_printf("[W] " fmt "\n", ##__VA_ARGS__)
#define RTT_LOG_E(fmt, ...)  rtt_log_printf("[E] " fmt "\n", ##__VA_ARGS__)
#define RTT_LOG_D(fmt, ...)  rtt_log_printf("[D] " fmt "\n", ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* RTT_LOG_H__ */
