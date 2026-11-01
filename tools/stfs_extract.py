#!/usr/bin/env python3
"""Minimal STFS (CON/LIVE/PIRS) extractor for Xbox 360 packages."""
import struct, sys, os

def u24le(b): return b[0] | b[1] << 8 | b[2] << 16
def u24be(b): return b[0] << 16 | b[1] << 8 | b[2]

class STFS:
    def __init__(self, path):
        self.f = open(path, 'rb')
        h = self.f.read(0x1000)
        self.magic = h[:4]
        assert self.magic in (b'CON ', b'LIVE', b'PIRS'), self.magic
        self.header_size = struct.unpack('>I', h[0x340:0x344])[0]
        vd = h[0x379:0x379 + 0x24]
        self.vflags = vd[2]
        self.ft_blocks = struct.unpack('<H', vd[3:5])[0]
        self.ft_start = u24le(vd[5:8])
        self.total_alloc = struct.unpack('>I', vd[0x1C:0x20])[0]
        first = (self.header_size + 0xFFF) & 0xF000
        self.first_table = first
        self.shift = 0 if (first >> 12) == 0xB else 1
        self.spacing = [0xAB, 0x718F, 0xFE7DA] if self.shift == 0 else [0xAC, 0x723A, 0xFD00B]

    def block_off(self, n):
        adj = 0
        if n >= 0xAA:
            adj += ((n // 0xAA) + 1) << self.shift
        if n > 0x70E4:
            adj += ((n // 0x70E4) + 1) << self.shift
        return 0xC000 + (adj + n) * 0x1000

    def hash_entry(self, n):
        rec = n % 0xAA
        t = (n // 0xAA) * self.spacing[0]
        if n >= 0xAA:
            t += ((n // 0x70E4) + 1) << self.shift
            if n >= 0x70E4:
                t += 1 << self.shift
        off = self.first_table + t * 0x1000 + rec * 0x18
        self.f.seek(off)
        e = self.f.read(0x18)
        return e[0x14], u24be(e[0x15:0x18])

    def read_chain(self, start, count, consecutive):
        out, n = [], start
        for i in range(count):
            self.f.seek(self.block_off(n)); out.append(self.f.read(0x1000))
            if i + 1 < count:
                n = n + 1 if consecutive else self.hash_entry(n)[1]
        return b''.join(out)

    def entries(self):
        ft = self.read_chain(self.ft_start, self.ft_blocks, False)
        res = []
        for i in range(0, len(ft), 0x40):
            e = ft[i:i + 0x40]
            nl = e[0x28] & 0x3F
            if nl == 0: continue
            res.append(dict(idx=i // 0x40, name=e[:nl].decode('ascii', 'replace'),
                            dir=bool(e[0x28] & 0x80), cons=bool(e[0x28] & 0x40),
                            blocks=u24le(e[0x29:0x2C]), start=u24le(e[0x2F:0x32]),
                            parent=struct.unpack('>h', e[0x32:0x34])[0],
                            size=struct.unpack('>I', e[0x34:0x38])[0]))
        return res

    def extract(self, outdir):
        ents = self.entries(); byidx = {e['idx']: e for e in ents}
        def path(e):
            p = [e['name']]
            while e['parent'] != -1 and e['parent'] in byidx:
                e = byidx[e['parent']]; p.insert(0, e['name'])
            return os.path.join(outdir, *p)
        for e in ents:
            p = path(e)
            if e['dir']:
                os.makedirs(p, exist_ok=True); continue
            os.makedirs(os.path.dirname(p), exist_ok=True)
            data = self.read_chain(e['start'], e['blocks'], e['cons'])[:e['size']]
            open(p, 'wb').write(data)
            print(f"{e['size']:>12}  {'C' if e['cons'] else '-'}  {os.path.relpath(p, outdir)}")

if __name__ == '__main__':
    s = STFS(sys.argv[1])
    print(f"magic={s.magic} header_size={s.header_size:#x} shift={s.shift} ft_start={s.ft_start} ft_blocks={s.ft_blocks} alloc={s.total_alloc}")
    s.extract(sys.argv[2])
