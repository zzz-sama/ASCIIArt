"""Generate a small uncompressed AVI for testing ASCIIArt video playback.

This is a developer helper, not part of the application.  It writes an
uncompressed 24-bit DIB AVI that Windows Media Foundation can decode without
any third-party codec.
"""

from __future__ import annotations

import struct
from pathlib import Path

import numpy as np

WIDTH = 320
HEIGHT = 180
FPS = 24
SECONDS = 3
OUT = Path(__file__).resolve().parents[1] / "test-assets" / "test-video.avi"


def chunk(fourcc: bytes, data: bytes) -> bytes:
    payload = fourcc + struct.pack("<I", len(data)) + data
    if len(data) & 1:
        payload += b"\0"
    return payload


def list_chunk(kind: bytes, data: bytes) -> bytes:
    size = 4 + len(data)
    payload = b"LIST" + struct.pack("<I", size) + kind + data
    if size & 1:
        payload += b"\0"
    return payload


def make_frame(index: int, total: int) -> bytes:
    yy, xx = np.mgrid[0:HEIGHT, 0:WIDTH].astype(np.float32)
    u = xx / (WIDTH - 1)
    v = yy / (HEIGHT - 1)
    t = index / max(1, total - 1)

    frame = np.empty((HEIGHT, WIDTH, 3), dtype=np.uint8)
    frame[:, :, 0] = np.clip(20 + 180 * u + 30 * np.sin(t * 6.28), 0, 255)
    frame[:, :, 1] = np.clip(35 + 150 * v + 40 * np.cos(t * 6.28), 0, 255)
    frame[:, :, 2] = np.clip(90 + 90 * (1 - u) + 50 * np.sin((u + v + t) * 6.28), 0, 255)

    cx = WIDTH * (0.15 + 0.7 * t)
    cy = HEIGHT * (0.5 + 0.28 * np.sin(t * 6.28 * 2))
    radius = 32
    mask = (xx - cx) ** 2 + (yy - cy) ** 2 <= radius**2
    frame[mask] = (255, 210, 60)

    bar_top = HEIGHT - 38
    frame[bar_top:, :, :] = (235, 235, 235)
    moving = int((WIDTH - 90) * (0.5 + 0.5 * np.sin(t * 6.28)))
    frame[bar_top + 8 : bar_top + 30, 20 + moving : 20 + moving + 70] = (25, 25, 25)
    stripe = np.clip(
        30 + 200 * u[bar_top + 12 : bar_top + 26, 24 : WIDTH - 24], 0, 255
    )
    frame[bar_top + 12 : bar_top + 26, 24 : WIDTH - 24] = np.stack(
        (stripe, stripe, stripe), axis=-1
    )
    return frame


def build() -> None:
    stride = ((WIDTH * 3 + 3) // 4) * 4
    frame_size = stride * HEIGHT
    total = FPS * SECONDS
    frame_data = []
    for i in range(total):
        rgb = make_frame(i, total)
        bgr_bottom_up = np.flipud(rgb[:, :, ::-1])
        row = bgr_bottom_up.tobytes()
        if len(row) != stride * HEIGHT:
            raise RuntimeError("unexpected frame size")
        frame_data.append(row)

    avih = struct.pack(
        "<14I",
        int(1_000_000 / FPS),  # microseconds per frame
        frame_size * FPS,      # max bytes per second
        0,                     # padding granularity
        0x10,                  # AVIF_HASINDEX
        total,
        0,
        1,
        frame_size,
        WIDTH,
        HEIGHT,
        0,
        0,
        0,
        0,
    )
    strh = (
        b"vids"
        + b"DIB "
        + struct.pack("<IHH", 0, 0, 0)  # flags, priority, language
        + struct.pack(
            "<8I",
            0,  # initial frames
            1,  # scale
            FPS,
            0,  # start
            total,
            frame_size,
            0xFFFFFFFF,
            0,  # sample size (video)
        )
        + struct.pack("<4h", 0, 0, WIDTH, HEIGHT)
    )
    if len(strh) != 56:
        raise RuntimeError(f"strh is {len(strh)} bytes")
    strf = struct.pack(
        "<IiiHHIIiiII",
        40,
        WIDTH,
        HEIGHT,
        1,
        24,
        0,  # BI_RGB
        frame_size,
        0,
        0,
        0,
        0,
    )
    if len(strf) != 40:
        raise RuntimeError(f"strf is {len(strf)} bytes")

    hdrl = list_chunk(b"hdrl", chunk(b"avih", avih) + list_chunk(b"strl", chunk(b"strh", strh) + chunk(b"strf", strf)))

    movi_chunks = b"".join(chunk(b"00db", data) for data in frame_data)
    movi = list_chunk(b"movi", movi_chunks)

    offsets = []
    offset = 4
    for data in frame_data:
        offsets.append(offset)
        offset += 8 + len(data) + (len(data) & 1)
    idx1_payload = b"".join(
        struct.pack("<4sIII", b"00db", 0x10, off, len(data))
        for off, data in zip(offsets, frame_data)
    )
    idx1 = chunk(b"idx1", idx1_payload)

    body = b"AVI " + hdrl + movi + idx1
    riff = b"RIFF" + struct.pack("<I", len(body)) + body
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(riff)
    print(f"wrote {OUT} ({len(riff)} bytes, {total} frames)")


if __name__ == "__main__":
    build()
