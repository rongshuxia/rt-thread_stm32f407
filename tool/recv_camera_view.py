"""TCP client: receive JPEG stream and display live preview (Pillow + tkinter)."""

import io
import socket
import sys
import tkinter as tk

from PIL import Image, ImageTk

HOST = "192.168.188.18"
PORT = 8001
SCALE = 2  # display scale: 320x240 -> 640x480
TITLE = "Network Camera (close window or Ctrl+C to quit)"

_RESAMPLE = getattr(getattr(Image, "Resampling", Image), "LANCZOS", Image.BICUBIC)


def split_jpeg(buf):
    frames = []
    start = 0
    while True:
        soi = buf.find(b"\xff\xd8", start)
        if soi < 0:
            return frames, buf[start:]
        eoi = buf.find(b"\xff\xd9", soi + 2)
        if eoi < 0:
            return frames, buf[soi:]
        frames.append(buf[soi : eoi + 2])
        start = eoi + 2
    return frames, b""


class CameraViewer:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title(TITLE)
        self.root.geometry(f"{320 * SCALE}x{240 * SCALE + 40}")

        self.image_label = tk.Label(self.root, bg="black")
        self.image_label.pack(expand=True, fill="both")

        self.status = tk.Label(self.root, text="Connecting...", anchor="w")
        self.status.pack(fill="x", padx=8, pady=4)

        self.photo = None
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
        img = Image.open(io.BytesIO(jpeg))
        src_w, src_h = img.size
        disp_w = src_w * SCALE
        disp_h = src_h * SCALE
        img = img.resize((disp_w, disp_h), _RESAMPLE)
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
    sock.settimeout(30)

    try:
        viewer.set_status(f"Connecting to {HOST}:{PORT} ...")
        sock.connect((HOST, PORT))
        viewer.set_status("Connected. Waiting for first frame...")
    except OSError as exc:
        print(f"Connect failed: {exc}", file=sys.stderr)
        print("Check: board powered, same subnet, port 8001 listening.", file=sys.stderr)
        viewer.stop()
        return 1

    sock.settimeout(10)
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

            if not frames and viewer.frame_count == 0:
                viewer.set_status(f"Receiving... {total_bytes} bytes (waiting for JPEG frame)")

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
