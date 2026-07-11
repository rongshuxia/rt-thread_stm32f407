import socket, os, time

HOST, PORT = "192.168.188.18", 8001
os.makedirs("frames", exist_ok=True)

def split_jpeg(buf):
    frames, start = [], 0
    while True:
        soi = buf.find(b"\xff\xd8", start)
        if soi < 0: return frames, buf[start:]
        eoi = buf.find(b"\xff\xd9", soi + 2)
        if eoi < 0: return frames, buf[soi:]
        frames.append(buf[soi:eoi+2])
        start = eoi + 2
    return frames, b""

s = socket.socket()
s.settimeout(30)          # 等板子 1s 延迟 + 首帧
print("连接中...")
s.connect((HOST, PORT))
print("已连接，等待数据...")

buf, idx = b"", 0
while True:
    data = s.recv(8192)
    if not data:
        print("连接断开"); break
    buf += data
    frames, buf = split_jpeg(buf)
    for f in frames:
        path = f"frames/frame_{idx:05d}.jpg"
        open(path, "wb").write(f)
        print(f"收到 {len(f)} bytes -> {path}")
        idx += 1