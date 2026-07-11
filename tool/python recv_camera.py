"""
STM32 网络摄像头 TCP 客户端 —— 保存 JPEG 帧到本地。

板端协议：
  - 板子为 TCP Server，PC 为 Client
  - 端口 8001，无自定义帧头/长度字段
  - 每帧为标准 JPEG 裸数据，帧间首尾相接
  - 帧起始标记 SOI: 0xFF 0xD8
  - 帧结束标记 EOI: 0xFF 0xD9
  - 图像分辨率：320 x 240（JPEG 压缩后约 5~15 KB/帧）

用法：
  python "python recv_camera.py"
  Ctrl+C 停止，图片保存在 frames/ 目录
"""

import os
import socket

# 板子 IP 与 TCP 端口（与 eth_camera.h 中 ETH_PORT 一致）
HOST = "192.168.188.18"
PORT = 8001

# 保存目录（相对当前工作目录）
OUT_DIR = "frames"


def split_jpeg(buf):
    """
    从 TCP 字节流中切出完整 JPEG 帧。

    TCP 是流式协议，一次 recv() 可能收到半帧或多帧，
    因此需要缓存 buf，按 SOI/EOI 标记切分。

    参数:
        buf: 累积的原始字节流
    返回:
        frames: 已完整的 JPEG 帧列表
        remain: 未凑齐一帧的剩余字节（留到下次 recv 继续拼）
    """
    frames = []
    start = 0

    while True:
        # 找帧头 FF D8
        soi = buf.find(b"\xff\xd8", start)
        if soi < 0:
            return frames, buf[start:]

        # 从帧头之后找帧尾 FF D9
        eoi = buf.find(b"\xff\xd9", soi + 2)
        if eoi < 0:
            # 只有帧头没有帧尾，说明帧还没收完
            return frames, buf[soi:]

        # 截取完整一帧（含 EOI 两字节）
        frames.append(buf[soi : eoi + 2])
        start = eoi + 2

    return frames, b""


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    sock = socket.socket()
    # 连接后板子约 1s 才开始发首帧，超时设长一些
    sock.settimeout(30)

    print(f"连接 {HOST}:{PORT} ...")
    sock.connect((HOST, PORT))
    print("已连接，接收中（Ctrl+C 停止）...")

    buf = b""
    idx = 0

    try:
        while True:
            data = sock.recv(8192)
            if not data:
                print("板端关闭连接")
                break

            buf += data
            frames, buf = split_jpeg(buf)

            for jpeg in frames:
                path = os.path.join(OUT_DIR, f"frame_{idx:05d}.jpg")
                with open(path, "wb") as f:
                    f.write(jpeg)
                print(f"保存 {path}  ({len(jpeg)} bytes)")
                idx += 1

    except KeyboardInterrupt:
        print(f"\n停止，共保存 {idx} 帧")
    finally:
        sock.close()


if __name__ == "__main__":
    main()
