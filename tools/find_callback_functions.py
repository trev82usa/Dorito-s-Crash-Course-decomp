#!/usr/bin/env python3
"""
Find functions that ReXGlue's discovery missed: code addresses that the game
loads as constants (lis/addi or lis/ori pairs) and later calls through a
pointer, e.g. sort comparators and thread entry points. Leaf functions like
these often have no .pdata entry and no direct `bl` callers.

usage: find_callback_functions.py <image.bin> <generated/default/*_init.cpp> [out.toml]

image.bin is the decrypted, decompressed XEX image (tools/xexdump.cpp).
Prints a [functions] TOML fragment for the codegen config.
"""
import bisect
import re
import struct
import sys

IMG_BASE = 0x82000000
TEXT_START, TEXT_END = 0x821A0000, 0x829F768C
PDATA_START, PDATA_END = 0x82170400, 0x82191698

# Constant-loaded code addresses that are not function entries.
EXCLUDE = {
    0x825E27E8,  # exception-return path in the middle of a function epilogue
}


def main(image_path, init_cpp, out_path=None):
    img = open(image_path, 'rb').read()

    def word(a):
        return struct.unpack('>I', img[a - IMG_BASE:a - IMG_BASE + 4])[0]

    known = sorted(int(m, 16) for m in re.findall(r'\{\s*0x([0-9A-F]+),\s*sub_', open(init_cpp).read()))
    known_set = set(known)

    def containing(a):
        i = bisect.bisect_right(known, a) - 1
        return known[i] if i >= 0 else None

    pdata = set()
    for o in range(PDATA_START - IMG_BASE, PDATA_END - IMG_BASE, 8):
        b, _ = struct.unpack('>II', img[o:o + 8])
        if b:
            pdata.add(b)

    found = {}
    for a in range(TEXT_START, TEXT_END, 4):
        w = word(a)
        if (w >> 26) != 15 or ((w >> 16) & 31) != 0:      # lis rD, hi  (addis rD, 0, hi)
            continue
        rd = (w >> 21) & 31
        hi = w & 0xFFFF
        for k in range(1, 10):
            v = word(a + 4 * k)
            op = v >> 26
            ra = (v >> 16) & 31
            rs = (v >> 21) & 31
            if op == 14 and ra == rd:                     # addi rX, rD, lo
                lo = v & 0xFFFF
                target = ((hi << 16) + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF
            elif op == 24 and rs == rd:                   # ori rX, rD, lo
                target = (hi << 16) | (v & 0xFFFF)
            else:
                # stop if rD is overwritten or control flow leaves the block
                if op in (16, 18, 19):
                    break
                if ((v >> 21) & 31) == rd and op not in (36, 37, 38, 44, 45, 62, 52, 53, 54, 55):
                    break
                continue
            if not (TEXT_START <= target < TEXT_END) or target & 3 or target in known_set:
                break
            # A target inside the referencing function is a jump-table base or
            # local label, not a callback.
            if containing(target) == containing(a) or target in EXCLUDE:
                break
            prev = word(target - 4)
            boundary = (prev == 0x4E800020                 # blr
                        or prev == 0x4E800420              # bctr (thunk tails)
                        or prev == 0
                        or ((prev >> 26) == 18 and (prev & 1) == 0)   # b (no link)
                        or target in pdata)
            if boundary:
                found.setdefault(target, []).append(a + 4 * k)
            break

    # Virtual-call thunks: lwz rA,0(rObj); lwz rB,off(rA); mtctr rB; bctr.
    # MSVC emits runs of these back to back; each is a complete function.
    thunks = set()
    for a in range(TEXT_START, TEXT_END - 12, 4):
        w0, w1, w2, w3 = (word(a + i) for i in range(0, 16, 4))
        if (w0 >> 26) != 32 or (w0 & 0xFFFF) != 0:                  # lwz rA, 0(rObj)
            continue
        ra = (w0 >> 21) & 31
        if (w1 >> 26) != 32 or ((w1 >> 16) & 31) != ra:            # lwz rB, off(rA)
            continue
        rb = (w1 >> 21) & 31
        if w2 != (0x7C0903A6 | (rb << 21)) or w3 != 0x4E800420 or a in known_set:
            continue
        # The same four instructions also end ordinary functions (a virtual
        # tail call). Only a sequence that follows a terminator is a thunk.
        prev = word(a - 4)
        if (prev in (0x4E800020, 0x4E800420, 0)
                or ((prev >> 26) == 18 and (prev & 1) == 0)
                or a in pdata):
            thunks.add(a)

    lines = ['# Functions reached only through pointers (found by tools/find_callback_functions.py)\n',
             '[functions]\n']
    for t in sorted(thunks):
        lines.append(f'0x{t:08X} = {{ size = 0x10 }}  # virtual-call thunk\n')
    for t in sorted(found):
        if t in thunks:
            continue
        refs = ', '.join(f'0x{r:08X}' for r in found[t][:3])
        lines.append(f'0x{t:08X} = {{}}  # referenced at {refs}\n')
    text = ''.join(lines)
    if out_path:
        open(out_path, 'w').write(text)
    print(f'{len(found)} missing function entry points', file=sys.stderr)
    print(text)


if __name__ == '__main__':
    main(*sys.argv[1:])
