#ifndef __NET_THROUGHPUT_H__
#define __NET_THROUGHPUT_H__

#define NETPERF_PORT_DEFAULT        5002
#define NETPERF_DURATION_SEC        10
#define NETPERF_ACCEPT_TIMEOUT_SEC  60

int netperf_tcp_server(int port, int duration_sec);
int netperf_tcp_client(const char *host, int port, int duration_sec);

#endif
