# PSARC reader: python psarc.py <archive> list | extract <name-substring> <out_dir>
import os, struct, sys, zlib


class Psarc:
    def __init__(self, path):
        self.f = open(path, "rb")
        h = self.f.read(32)
        assert h[:4] == b"PSAR" and h[8:12] == b"zlib", h[:12]
        toc_len, entry_size, count, self.block, _flags = struct.unpack(">IIIII", h[12:32])
        toc = self.f.read(toc_len - 32)
        self.entries = []
        for i in range(count):
            e = toc[i * entry_size:(i + 1) * entry_size]
            self.entries.append((struct.unpack(">I", e[16:20])[0],
                                 int.from_bytes(e[20:25], "big"), int.from_bytes(e[25:30], "big")))
        width = 2 if self.block <= 0x10000 else 3 if self.block <= 0x1000000 else 4
        table = toc[count * entry_size:]
        self.sizes = [int.from_bytes(table[i:i + width], "big") for i in range(0, len(table) - width + 1, width)]
        self.names = [""] + self.read(0).decode("utf-8", "replace").split("\n")

    def read(self, index):
        block, size, offset = self.entries[index]
        out = bytearray()
        self.f.seek(offset)
        while len(out) < size:
            n = self.sizes[block] or self.block
            data = self.f.read(n)
            if n == self.block and (len(out) + n <= size) and data[:1] != b"\x78":
                out += data  # stored block
            else:
                try:
                    out += zlib.decompress(data)
                except zlib.error:
                    out += data
            block += 1
        return bytes(out[:size])


if __name__ == "__main__":
    p = Psarc(sys.argv[1])
    if sys.argv[2] == "list":
        for i, n in enumerate(p.names[1:], 1):
            print(p.entries[i][1], n)
    else:
        for i, n in enumerate(p.names[1:], 1):
            if sys.argv[3] in n:
                dst = os.path.join(sys.argv[4], n.lstrip("/"))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                open(dst, "wb").write(p.read(i))
