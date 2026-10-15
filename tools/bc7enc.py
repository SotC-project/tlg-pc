# Minimal BC7 encoder (mode 6 only: one subset, RGBA 7.7.7.7 endpoints + p-bit, 4-bit indices).
# Good enough for flat UI art such as button prompts. encode(rgba: HxWx4 uint8) -> bytes.
import numpy as np


def _encode_block(px):
    px = px.reshape(16, 4).astype(np.float64)
    lo, hi = px.min(0), px.max(0)
    best = None
    # Endpoints: bounding box corners, quantised to 7 bits + shared p-bit per endpoint.
    for p0 in (0, 1):
        for p1 in (0, 1):
            e0 = np.clip(np.round((lo - p0) / 2), 0, 127).astype(int)
            e1 = np.clip(np.round((hi - p1) / 2), 0, 127).astype(int)
            c0 = e0 * 2 + p0
            c1 = e1 * 2 + p1
            w = np.array([0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64])
            pal = ((64 - w)[:, None] * c0 + w[:, None] * c1 + 32) >> 6
            d = ((px[:, None, :] - pal[None, :, :]) ** 2).sum(2)
            idx = d.argmin(1)
            err = d[np.arange(16), idx].sum()
            if best is None or err < best[0]:
                best = (err, e0, e1, p0, p1, idx)
    _, e0, e1, p0, p1, idx = best
    # The first index's top bit is implicit 0: swap endpoints when it is set.
    if idx[0] & 8:
        e0, e1, p0, p1, idx = e1, e0, p1, p0, 15 - idx
    bits = 1 << 6  # mode 6
    pos = 7
    for c in range(4):
        for e in (e0, e1):
            bits |= int(e[c]) << pos
            pos += 7
    bits |= p0 << pos
    pos += 1
    bits |= p1 << pos
    pos += 1
    bits |= int(idx[0]) << pos
    pos += 3
    for i in range(1, 16):
        bits |= int(idx[i]) << pos
        pos += 4
    return bits.to_bytes(16, "little")


def encode(rgba):
    h, w = rgba.shape[:2]
    out = bytearray()
    for y in range(0, h, 4):
        for x in range(0, w, 4):
            out += _encode_block(rgba[y:y + 4, x:x + 4])
    return bytes(out)


if __name__ == "__main__":
    import texture2ddecoder
    rng = np.random.default_rng(1)
    img = np.zeros((16, 16, 4), np.uint8)
    img[:8, :, :] = (200, 40, 30, 255)
    img[8:, :, 3] = 0
    img[4:12, 4:12] = rng.integers(0, 255, (8, 8, 4))
    dec = np.frombuffer(texture2ddecoder.decode_bc7(encode(img), 16, 16), np.uint8).reshape(16, 16, 4)
    dec = dec[..., [2, 1, 0, 3]]
    flat = np.abs(dec[:4, :4].astype(int) - img[:4, :4]).max()
    assert flat <= 2, flat
    print("ok, mean error", np.abs(dec.astype(int) - img).mean())
