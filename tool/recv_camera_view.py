"""
STM32 网络摄像头 TCP 客户端 —— 实时预览 JPEG 流。

板端协议：
  - 板子为 TCP Server，PC 为 Client，端口 8001
  - 无自定义帧头，连续发送标准 JPEG 裸数据
  - 帧边界：SOI(FF D8) ~ EOI(FF D9)
  - 分辨率 320x240，依赖 Pillow 解码，tkinter 显示

用法：
  python recv_camera_view.py
  关闭窗口或 Ctrl+C 退出

依赖：
  pip install pillow   （tkinter 随 Python 自带）
"""

import io
import socket
import sys
import tkinter as tk

from PIL import Image, ImageTk

# ---------- 连接参数 ----------
HOST = "192.168.188.18"   # 板子静态 IP（rtconfig.h）
PORT = 8001               # TCP 端口（eth_camera.h ETH_PORT）
SCALE = 2                 # 显示放大倍数，2 表示 320x240 -> 640x480
TITLE = "Network Camera (close window or Ctrl+C to quit)"
MAX_JPEG_BYTES = 36 * 1024   # 板端 JPEG_BUF_SIZE=32KB，留少量余量
STALE_BUF_LIMIT = 48 * 1024  # 半帧超过此大小则丢弃重同步

# Pillow 缩放算法（兼容不同版本）
_RESAMPLE = getattr(getattr(Image, "Resampling", Image), "LANCZOS", Image.BICUBIC)


def split_jpeg(buf):
    """
    从 TCP 字节流中切出完整 JPEG 帧。

    参数:
        buf: 累积接收的原始字节
    返回:
        (frames, remain) — 完整帧列表 + 未收齐的尾部字节
    """
    frames = []
    start = 0

    while True:
        soi = buf.find(b"\xff\xd8", start)   # JPEG 帧头
        if soi < 0:
            return frames, buf[start:]

        eoi = buf.find(b"\xff\xd9", soi + 2)  # JPEG 帧尾
        if eoi < 0:
            if len(buf) - soi > STALE_BUF_LIMIT:
                start = soi + 2
                continue
            return frames, buf[soi:]

        frame = buf[soi : eoi + 2]
        if len(frame) > MAX_JPEG_BYTES:
            start = eoi + 2
            continue

        frames.append(frame)
        start = eoi + 2

    return frames, b""


class CameraViewer:
    """tkinter 窗口，负责解码 JPEG 并刷新显示。"""

    def __init__(self):
        self.root = tk.Tk()
        self.root.title(TITLE)
        # 窗口大小按 320x240 和放大倍数估算
        self.root.geometry(f"{320 * SCALE}x{240 * SCALE + 40}")

        self.image_label = tk.Label(self.root, bg="black")
        self.image_label.pack(expand=True, fill="both")

        self.status = tk.Label(self.root, text="Connecting...", anchor="w")
        self.status.pack(fill="x", padx=8, pady=4)

        self.photo = None       # 必须持有引用，否则 tkinter 会回收图像
        self.frame_count = 0
        self.running = True
        self.root.protocol("WM_DELETE_WINDOW", self.stop)

    def stop(self):
        self.running = False
        self.root.destroy()

    def set_status(self, text):
        self.status.config(text=text)
        self.root.update_idletasks()

    def show_jpeg(self, jpeg):
        """解码一帧 JPEG 并放大显示。"""
        img = Image.open(io.BytesIO(jpeg))
        src_w, src_h = img.size

        # 放大到屏幕尺寸
        img = img.resize((src_w * SCALE, src_h * SCALE), _RESAMPLE)

        self.photo = ImageTk.PhotoImage(img)
        self.image_label.config(image=self.photo)
        self.frame_count += 1
        self.set_status(
            f"Frame {self.frame_count}  |  {src_w}x{src_h} x{SCALE}  |  {len(jpeg)} bytes"
        )
        self.root.update()


def main():
    viewer = CameraViewer()

    sock = socket.socket()
    sock.settimeout(30)  # 首次连接等待板子 accept + 初始化

    try:
        viewer.set_status(f"Connecting to {HOST}:{PORT} ...")
        sock.connect((HOST, PORT))
        viewer.set_status("Connected. Waiting for first frame...")
    except OSError as exc:
        print(f"Connect failed: {exc}", file=sys.stderr)
        print("Check: board powered, same subnet, port 8001 listening.", file=sys.stderr)
        viewer.stop()
        return 1

    sock.settimeout(10)  # 连接成功后，10s 无数据则提示
    buf = b""
    total_bytes = 0

    try:
        while viewer.running:
            try:
                data = sock.recv(8192)
            except socket.timeout:
                viewer.set_status("Timeout: no data in 10s (board still sending?)")
                continue

            if not data:
                viewer.set_status("Connection closed by board.")
                break

            total_bytes += len(data)
            buf += data
            frames, buf = split_jpeg(buf)

            if not frames and len(buf) > STALE_BUF_LIMIT:
                soi = buf.rfind(b"\xff\xd8")
                buf = buf[soi:] if soi >= 0 else b""

            if not frames and viewer.frame_count == 0:
                viewer.set_status(
                    f"Receiving... {total_bytes} bytes (waiting for JPEG frame)"
                )

            for jpeg in frames:
                try:
                    viewer.show_jpeg(jpeg)
                except Exception as exc:
                    print(f"Decode failed ({len(jpeg)} bytes): {exc}", file=sys.stderr)

    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        sock.close()
        if viewer.running:
            viewer.stop()

    print(f"Done. {viewer.frame_count} frames, {total_bytes} bytes total.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
