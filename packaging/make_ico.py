#!/usr/bin/env python3
# ======================================================================
#  make_ico.py  -  生成 resources/winlauncher.ico（exe 文件图标）
#
#  为什么要有这个脚本:
#    程序里的托盘/窗口图标是 main.cpp 里 makeAppIcon() 用 QPainter 现画的,
#    但 **exe 文件本身的图标是 PE 资源(RT_ICON)**, 只能由 .ico + .rc 提供。
#    没有 .ico 时资源管理器/任务栏显示的是空白通用图标 —— 就是"exe 没图标"。
#
#  图案与 makeAppIcon() 完全一致(2x2 圆角方块, #007AFF, 下排半透明),
#  只换了渲染方式: 4x 超采样抗锯齿 -> 下采样, 小尺寸也清晰。
#
#  产物: resources/winlauncher.ico (16/24/32/48/64/128 BMP + 256 PNG)
#  用法: python packaging/make_ico.py
# ======================================================================
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "resources" / "winlauncher.ico"

# 与 main.cpp makeAppIcon() 保持一致的几何/配色（基准画布 64x64）
BASE = 64.0
CELL = 24.0
GAP = 6.0
RADIUS = 7.0
BLUE = (0, 122, 255)
ALPHA_ROWS = (255, 190)  # 上排不透明, 下排略透

# 小尺寸用 BMP(DIB) 编码兼容性最好, 大尺寸用 PNG 省空间(Windows Vista+)
SIZES = [16, 24, 32, 48, 64, 128, 256]
PNG_FROM = 128
SS = 4  # 超采样倍数


def render(size: int):
    """返回 size*size 的 RGBA 像素(bytes), 行优先, 自上而下。"""
    n = size * SS
    u = n / BASE
    cell, gap, rad = CELL * u, GAP * u, RADIUS * u
    start = (n - (cell * 2 + gap)) / 2.0

    rects = []  # (x0, y0, w, h, alpha)
    for row in range(2):
        for col in range(2):
            rects.append((start + col * (cell + gap), start + row * (cell + gap),
                          cell, cell, ALPHA_ROWS[row]))

    # 超采样累积 alpha
    acc = [0] * (n * n)
    for (x0, y0, w, h, a) in rects:
        x_lo, x_hi = int(max(0, x0 - 1)), int(min(n, x0 + w + 1))
        y_lo, y_hi = int(max(0, y0 - 1)), int(min(n, y0 + h + 1))
        rx_lo, rx_hi = x0 + rad, x0 + w - rad
        ry_lo, ry_hi = y0 + rad, y0 + h - rad
        for py in range(y_lo, y_hi):
            cy = py + 0.5
            if cy < y0 or cy > y0 + h:
                continue
            kcy = min(max(cy, ry_lo), ry_hi)
            row_off = py * n
            for px in range(x_lo, x_hi):
                cx = px + 0.5
                if cx < x0 or cx > x0 + w:
                    continue
                kcx = min(max(cx, rx_lo), rx_hi)
                dx, dy = cx - kcx, cy - kcy
                if dx * dx + dy * dy <= rad * rad:
                    acc[row_off + px] += a

    # 下采样 -> RGBA（直通 alpha, 颜色恒为 BLUE, 只让不透明度渐变）
    px_out = bytearray(size * size * 4)
    area = SS * SS
    for y in range(size):
        for x in range(size):
            s = 0
            for sy in range(SS):
                base = (y * SS + sy) * n + x * SS
                for sx in range(SS):
                    s += acc[base + sx]
            a = s // area
            o = (y * size + x) * 4
            px_out[o] = BLUE[0]
            px_out[o + 1] = BLUE[1]
            px_out[o + 2] = BLUE[2]
            px_out[o + 3] = a
    return bytes(px_out)


def png_chunk(tag: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def to_png(size: int, rgba: bytes) -> bytes:
    raw = bytearray()
    stride = size * 4
    for y in range(size):
        raw.append(0)  # filter: none
        raw += rgba[y * stride:(y + 1) * stride]
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)  # 8bit RGBA
    return (b"\x89PNG\r\n\x1a\n"
            + png_chunk(b"IHDR", ihdr)
            + png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + png_chunk(b"IEND", b""))


def to_dib(size: int, rgba: bytes) -> bytes:
    """ICO 里的 BMP: BITMAPINFOHEADER + BGRA 自下而上 + 1bpp AND mask。"""
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0,
                         size * size * 4, 0, 0, 0, 0)
    xor = bytearray()
    for y in range(size - 1, -1, -1):
        row = rgba[y * size * 4:(y + 1) * size * 4]
        for x in range(0, len(row), 4):
            xor += bytes((row[x + 2], row[x + 1], row[x], row[x + 3]))
    # AND mask: 每行按 4 字节对齐, 1 = 透明
    mask_stride = ((size + 31) // 32) * 4
    mask = bytearray()
    for y in range(size - 1, -1, -1):
        bits = bytearray(mask_stride)
        for x in range(size):
            if rgba[(y * size + x) * 4 + 3] == 0:
                bits[x >> 3] |= 0x80 >> (x & 7)
        mask += bits
    return header + bytes(xor) + bytes(mask)


def main():
    images = []
    for size in SIZES:
        rgba = render(size)
        data = to_png(size, rgba) if size >= PNG_FROM else to_dib(size, rgba)
        images.append((size, data))
        print(f"  {size:3d}x{size:<3d}  {len(data):7d} bytes  "
              f"{'PNG' if size >= PNG_FROM else 'BMP'}")

    count = len(images)
    header = struct.pack("<HHH", 0, 1, count)
    offset = 6 + 16 * count
    entries, blobs = b"", b""
    for size, data in images:
        dim = 0 if size >= 256 else size
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32,
                               len(data), offset)
        offset += len(data)
        blobs += data

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(header + entries + blobs)
    print(f"written: {OUT}  ({OUT.stat().st_size / 1024:.1f} KB, {count} sizes)")


if __name__ == "__main__":
    main()
