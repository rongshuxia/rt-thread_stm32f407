"""
PC 端 TCP 吞吐率测试，配合板端 netperf 使用。

直接运行（默认）:
  python netperf_pc.py
  -> 向板子 192.168.188.18:5002 发送数据 10s

可选参数:
  python netperf_pc.py client [board_ip] [port] [sec]
  python netperf_pc.py server [port] [sec]
"""

import socket
import sys
import time

DEFAULT_HOST = "192.168.188.18"
DEFAULT_PORT = 5002
DEFAULT_SEC = 10
BUF_SIZE = 65536
CONNECT_RETRY_SEC = 60


def print_rate(tag, nbytes, elapsed):
    if elapsed <= 0:
        elapsed = 0.001
    mbps = nbytes * 8.0 / elapsed / 1_000_000.0
    print(f"{tag}: {nbytes} bytes in {elapsed:.3f} s, {mbps:.2f} Mbit/s")


def connect_with_retry(host, port, timeout_sec):
    deadline = time.time() + timeout_sec
    attempt = 0

    while time.time() < deadline:
        attempt += 1
        sock = socket.socket()
        sock.settimeout(2.0)
        try:
            print(f"Connecting to {host}:{port} (try {attempt}) ...")
            sock.connect((host, port))
            sock.settimeout(None)
            return sock
        except OSError as exc:
            print(f"  failed: {exc}")
            sock.close()
            time.sleep(1.0)

    raise TimeoutError(f"cannot connect to {host}:{port} within {timeout_sec}s")


def run_client(host, port, duration):
    sock = connect_with_retry(host, port, CONNECT_RETRY_SEC)
    print(f"Connected. Sending for {duration}s ...")

    buf = b"x" * BUF_SIZE
    total = 0
    start = time.time()
    last = start
    interval_total = 0

    while time.time() - start < duration:
        n = sock.send(buf)
        if n <= 0:
            break
        total += n
        now = time.time()
        if now - last >= 1.0:
            print_rate("TX interval", total - interval_total, now - last)
            interval_total = total
            last = now

    elapsed = time.time() - start
    print_rate("TX final", total, elapsed)
    sock.close()


def run_server(port, duration):
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", port))
    srv.listen(1)
    print(f"Listening on 0.0.0.0:{port}, waiting for board ...")

    conn, addr = srv.accept()
    print(f"Client {addr} connected, receiving for {duration}s ...")

    total = 0
    start = time.time()
    last = start
    interval_total = 0

    while time.time() - start < duration:
        data = conn.recv(BUF_SIZE)
        if not data:
            break
        total += len(data)
        now = time.time()
        if now - last >= 1.0:
            print_rate("RX interval", total - interval_total, now - last)
            interval_total = total
            last = now

    elapsed = time.time() - start
    print_rate("RX final", total, elapsed)
    conn.close()
    srv.close()


def main():
    # 无参数: 默认 client -> 板子 192.168.188.18:5002
    if len(sys.argv) < 2:
        print(f"Default: client {DEFAULT_HOST}:{DEFAULT_PORT} for {DEFAULT_SEC}s")
        run_client(DEFAULT_HOST, DEFAULT_PORT, DEFAULT_SEC)
        return 0

    mode = sys.argv[1].lower()
    if mode == "client":
        host = sys.argv[2] if len(sys.argv) >= 3 else DEFAULT_HOST
        port = int(sys.argv[3]) if len(sys.argv) >= 4 else DEFAULT_PORT
        duration = int(sys.argv[4]) if len(sys.argv) >= 5 else DEFAULT_SEC
        run_client(host, port, duration)
    elif mode == "server":
        port = int(sys.argv[2]) if len(sys.argv) >= 3 else DEFAULT_PORT
        duration = int(sys.argv[3]) if len(sys.argv) >= 4 else DEFAULT_SEC
        run_server(port, duration)
    else:
        print(__doc__)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
