#!/usr/bin/env python3
"""Lists/extracts Doritos Crash Course 2 data.pak (tongas_pack_v30000).

Entry table: u32 offset, u32 size, u32 flags, u32 name_len, name (little-endian); data is LZMA.
usage: pak_extract.py data.pak outdir [substring-filter]
"""
import lzma, os, struct, sys, zlib

def entries(d):
    # header: magic[18], u32 count, u32 table_size, then per entry:
    # u32 offset, u32 size, u32 flags, u32 name_len, name
    count = struct.unpack_from('<I', d, 18)[0]
    p = 26
    for _ in range(count):
        off, size, flags, n = struct.unpack_from('<IIII', d, p); p += 16
        name = d[p:p + n].decode('latin1'); p += n
        yield name, off, size, flags

def main(pak, out, filt=''):
    d = open(pak, 'rb').read()
    for name, off, size, flags in entries(d):
        if filt not in name: continue
        blob = d[off:off + size]
        if blob[:1] == b'\x5d' and len(blob) > 13:
            # LZMA "alone" stream: props, dict size, u64 size (may be -1)
            try:
                data = lzma.LZMADecompressor(lzma.FORMAT_ALONE).decompress(blob)
                how = 'lzma'
            except lzma.LZMAError:
                data = blob; how = 'raw'
        else:
            data = blob; how = 'raw'
        path = os.path.join(out, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, 'wb').write(data)
        print(f'{len(data):>9} {how:4} f={flags} {name}')

if __name__ == '__main__':
    main(*sys.argv[1:])
