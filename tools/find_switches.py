#!/usr/bin/env python3
"""
Jump-table finder for Xbox 360 XEX images whose compiler scheduling does not
match XenonAnalyse's fixed instruction patterns.

Method
------
Guarded tables: for every `cmplwi crN, rI, MAX` followed by `bgt/ble crN`, the
dispatch code is executed concretely once per case index (0..MAX), following
unconditional branches and falling through conditional ones, until an
`mtctr`/`bctr` pair. The resulting CTR values are the case labels. Stack and
base+displacement spills/reloads of the index are tracked symbolically.

Unguarded tables (no bounds check before dispatch): the dispatch block is
simulated from its table-address `lis`; the case count is inferred by reading
entries until one falls outside the containing function or overlaps another
known table. These are flagged in the report as inferred.

A table is emitted only when every label lies inside the containing function
and the index register still holds the raw index at the bctr (XenonRecomp emits
`switch (rI.u64)`). Containing function = the .pdata entry, or, when the code
is not covered by .pdata, the gap between neighbouring .pdata entries; such
gaps are emitted as explicit `functions` entries for the recompiler config.

Output: XenonAnalyse-compatible switch TOML, a functions TOML fragment, and a
plain-text report of everything rejected or inferred.
"""
import struct, sys, bisect, capstone
from capstone import ppc as P

IMG_BASE = 0x82000000
UNK = object()


class Fail(Exception):
    pass


def main(image_path, out_toml, out_funcs, out_report, text_start, text_end, pdata_start, pdata_end):
    img = open(image_path, 'rb').read()
    md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_64 | capstone.CS_MODE_BIG_ENDIAN)
    md.detail = True

    def word(a): return struct.unpack('>I', img[a - IMG_BASE:a - IMG_BASE + 4])[0]
    def half(a): return struct.unpack('>H', img[a - IMG_BASE:a - IMG_BASE + 2])[0]
    def byte(a): return img[a - IMG_BASE]
    def readable(a): return text_start - 0x1A0000 <= a and 0 <= a - IMG_BASE < len(img) - 4

    cache = {}
    def insn_at(a):
        if a not in cache:
            if len(cache) > 50000: cache.clear()
            r = list(md.disasm(img[a - IMG_BASE:a - IMG_BASE + 4], a))
            cache[a] = r[0] if r else None
        return cache[a]

    # ---- function ranges: .pdata entries, plus the gaps between them ----
    funcs = []
    for o in range(pdata_start - IMG_BASE, pdata_end - IMG_BASE, 8):
        b, f = struct.unpack('>II', img[o:o + 8])
        if b: funcs.append((b, b + ((f >> 8) & 0x3FFFFF) * 4))
    funcs.sort()
    starts = [f[0] for f in funcs]

    # direct call targets: code not covered by .pdata is split at these, both by
    # XenonRecomp's own discovery pass and here, so boundaries agree
    bl_targets = set()
    for a in range(text_start, text_end, 4):
        w = word(a)
        if (w >> 26) == 18 and (w & 3) == 1:          # bl (AA=0, LK=1)
            off = w & 0x03FFFFFC
            if off & 0x02000000: off -= 0x04000000
            t = a + off
            if text_start <= t < text_end: bl_targets.add(t)
    bl_sorted = sorted(bl_targets)

    def func_of(a):
        i = bisect.bisect_right(starts, a) - 1
        if i >= 0 and funcs[i][0] <= a < funcs[i][1]:
            return funcs[i], False
        lo = funcs[i][1] if i >= 0 else text_start
        hi = funcs[i + 1][0] if i + 1 < len(funcs) else text_end
        # narrow to the call-target-delimited region holding `a`
        j = bisect.bisect_right(bl_sorted, a) - 1
        if j >= 0 and lo <= bl_sorted[j] <= a: lo = bl_sorted[j]
        if j + 1 < len(bl_sorted) and bl_sorted[j + 1] < hi: hi = bl_sorted[j + 1]
        while lo < hi and word(lo) == 0: lo += 4  # skip leading padding
        return (lo, hi), True

    def rn(op): return md.reg_name(op.reg)
    def gpr(name):
        return int(name[1:]) if name and name.startswith('r') and name[1:].isdigit() else None

    def simulate(start, idx_reg, idx, mem_init=None, limit=48):
        regs = {idx_reg: idx}
        mem = dict(mem_init or {})
        base_ver = {}  # register -> version counter, invalidates symbolic memory keys
        ctr, clobbered, path, a = None, False, [], start
        for _ in range(limit):
            ins = insn_at(a)
            if ins is None: raise Fail('undecodable')
            path.append(a)
            m, ops = ins.mnemonic, ins.operands

            def R(i):
                n = gpr(rn(ops[i]))
                v = regs.get(n, UNK)
                if v is UNK: raise Fail(f'unknown r{n} used by {m} at {a:x}')
                return v

            def W(i, v):
                nonlocal clobbered
                n = gpr(rn(ops[i]))
                if n == idx_reg and v != idx: clobbered = True
                regs[n] = v if v is UNK else v & 0xFFFFFFFFFFFFFFFF
                base_ver[n] = base_ver.get(n, 0) + 1

            def memkey(op):
                b = gpr(rn(op)) if op.type == P.PPC_OP_REG else gpr(md.reg_name(op.mem.base))
                return (b, base_ver.get(b, 0), op.mem.disp)

            def soft(i, f):
                try: W(0, f())
                except Fail: W(0, UNK)

            nxt = a + 4
            if m == 'nop': pass
            elif m == 'lis': W(0, (ops[1].imm << 16) & 0xFFFFFFFF)
            elif m == 'li': W(0, ops[1].imm)
            elif m in ('addi', 'la'):
                if ops[1].type == P.PPC_OP_REG and rn(ops[1]) == 'r0': W(0, ops[2].imm)
                else: soft(0, lambda: R(1) + ops[2].imm)
            elif m == 'addis': soft(0, lambda: R(1) + (ops[2].imm << 16))
            elif m == 'ori': soft(0, lambda: R(1) | ops[2].imm)
            elif m == 'mr': soft(0, lambda: R(1))
            elif m == 'add': soft(0, lambda: R(1) + R(2))
            elif m == 'subf': soft(0, lambda: R(2) - R(1))
            elif m == 'slwi': soft(0, lambda: (R(1) << ops[2].imm) & 0xFFFFFFFF)
            elif m == 'srwi': soft(0, lambda: (R(1) & 0xFFFFFFFF) >> ops[2].imm)
            elif m == 'clrlwi': soft(0, lambda: R(1) & (0xFFFFFFFF >> ops[2].imm))
            elif m == 'clrldi': soft(0, lambda: R(1) & (0xFFFFFFFFFFFFFFFF >> ops[2].imm))
            elif m == 'extsw':
                def f():
                    v = R(1) & 0xFFFFFFFF
                    return v - (1 << 32) if v & 0x80000000 else v
                soft(0, f)
            elif m == 'rlwinm':
                def f():
                    v = R(1) & 0xFFFFFFFF; sh, mb, me = ops[2].imm, ops[3].imm, ops[4].imm
                    v = ((v << sh) | (v >> (32 - sh))) & 0xFFFFFFFF if sh else v
                    mask = 0
                    for bit in range(32):
                        if (mb <= me and mb <= bit <= me) or (mb > me and (bit >= mb or bit <= me)):
                            mask |= 1 << (31 - bit)
                    return v & mask
                soft(0, f)
            elif m == 'rldicl':
                def f():
                    v = R(1); sh, mb = ops[2].imm, ops[3].imm
                    v = ((v << sh) | (v >> (64 - sh))) & 0xFFFFFFFFFFFFFFFF if sh else v
                    return v & (0xFFFFFFFFFFFFFFFF >> mb)
                soft(0, f)
            elif m == 'mullw': soft(0, lambda: (R(1) & 0xFFFFFFFF) * (R(2) & 0xFFFFFFFF))
            elif m == 'mulli': soft(0, lambda: R(1) * ops[2].imm)
            elif m in ('lwzx', 'lhzx', 'lbzx'):
                def f():
                    ea = (R(1) + R(2)) & 0xFFFFFFFF
                    if not readable(ea): raise Fail(f'bad ea {ea:x}')
                    return {'lwzx': word, 'lhzx': half, 'lbzx': byte}[m](ea)
                soft(0, f)
            elif m in ('lwz', 'lhz', 'lbz', 'ld'):
                k = memkey(ops[1])
                W(0, mem.get(k, UNK))
            elif m in ('stw', 'sth', 'stb', 'std'):
                k = memkey(ops[1])
                n = gpr(rn(ops[0]))
                mem[k] = regs.get(n, UNK)
            elif m == 'mtctr': ctr = R(0) & 0xFFFFFFFF
            elif m == 'bctr':
                if ctr is None: raise Fail('bctr without ctr')
                return ctr, a, path, clobbered
            elif m == 'b': nxt = ops[0].imm
            elif m in ('bl', 'blr', 'bctrl', 'blrl'): raise Fail(f'{m} on dispatch path')
            elif m.startswith('b'): pass  # conditional: in-range path falls through
            elif m.startswith('cmp') or m.startswith('tw') or m.startswith('mf'):
                if m.startswith('mf') and ops: W(0, UNK)
            else:
                # unknown instruction: poison its destination if it has one
                if ops and ops[0].type == P.PPC_OP_REG and gpr(rn(ops[0])) is not None:
                    W(0, UNK)
            a = nxt
        raise Fail('no bctr within limit')

    def back_slots(cmp_addr, idx):
        """Index loaded from memory just before the compare: remember the slot."""
        init = {}
        for k in range(1, 8):
            ins = insn_at(cmp_addr - 4 * k)
            if not ins: break
            if ins.mnemonic in ('b', 'blr', 'bctr', 'bl'): break
            if ins.mnemonic in ('lwz', 'ld', 'lhz', 'lbz') and gpr(rn(ins.operands[0])) == idx:
                b = gpr(md.reg_name(ins.operands[1].mem.base))
                init[(b, 0, ins.operands[1].mem.disp)] = None  # placeholder: filled per-index
                return init
            if ins.operands and ins.operands[0].type == P.PPC_OP_REG and gpr(rn(ins.operands[0])) == idx:
                break
        return init

    tables, rejects, gap_funcs = {}, [], set()

    def finish(rec, fn_range, is_gap):
        labels = rec['labels']
        if any(l & 3 for l in labels):
            rec['reason'] = 'unaligned label'
        elif any(not (fn_range[0] <= l < fn_range[1]) for l in labels):
            rec['reason'] = 'label outside containing function'
        elif rec.get('clobbered'):
            rec['reason'] = f"index register r{rec['r']} clobbered before bctr"
        if 'reason' in rec:
            rejects.append(rec)
            return
        tables[rec['bctr']] = rec
        if is_gap: gap_funcs.add(fn_range)

    # ---- guarded tables ----
    for a in range(text_start, text_end, 4):
        w = word(a)
        if (w >> 26) != 10 or (w >> 21) & 1: continue          # cmplwi
        ins = insn_at(a)
        if not ins or ins.mnemonic != 'cmplwi' or len(ins.operands) != 3: continue
        crf = rn(ins.operands[0]); idx = gpr(rn(ins.operands[1])); mx = ins.operands[2].imm
        if idx is None or mx > 4096: continue
        guard = None
        for g in range(a + 4, a + 24, 4):
            gi = insn_at(g)
            if not gi: break
            if gi.mnemonic in ('bgt', 'ble', 'bgtlr', 'blelr'):
                gcr = rn(gi.operands[0]) if gi.operands and gi.operands[0].type == P.PPC_OP_REG else 'cr0'
                if gcr == crf: guard = gi; break
            if gi.mnemonic in ('b', 'blr', 'bctr', 'bl'): break
        if not guard: continue
        slots = back_slots(a, idx)
        try:
            labels, bctr_addr, path, clob = [], None, None, False
            for i in range(mx + 1):
                ctr, b, pth, c = simulate(a + 4, idx, i, {k: i for k in slots})
                if bctr_addr is None: bctr_addr, path = b, pth
                elif b != bctr_addr: raise Fail('cases reach different bctr')
                clob |= c
                labels.append(ctr)
        except Fail:
            continue
        if bctr_addr in tables: continue
        key = path[0]
        for k in range(len(path) - 1):
            if path[k + 1] != path[k] + 4: key = path[k + 1]
        gops = guard.operands
        default = gops[-1].imm if gops and gops[-1].type == P.PPC_OP_IMM else 0
        fn, gap = func_of(bctr_addr)
        finish(dict(base=key, r=idx, default=default, labels=labels, bctr=bctr_addr,
                    cmp=a, clobbered=clob, kind='guarded'), fn, gap)

    # ---- unguarded tables: dispatch blocks with no bound check ----
    table_addrs = set()
    for a in range(text_start, text_end, 4):
        if word(a) != 0x4E800420 or a in tables: continue
        if not any((word(a - 4 * k) >> 26) == 31 and ((word(a - 4 * k) >> 1) & 0x3FF) in (23, 279, 87)
                   for k in range(1, 8)):
            continue
        # locate `slwi/rlwinm rX, rI, ..` or direct lbzx index and the lis for the table
        start, idx = None, None
        for k in range(1, 10):
            ins = insn_at(a - 4 * k)
            if not ins: break
            if ins.mnemonic in ('b', 'blr', 'bctr', 'bl') or (ins.mnemonic.startswith('b') and k > 1 and ins.mnemonic != 'bctr'):
                break
            if ins.mnemonic in ('slwi', 'rlwinm') and idx is None:
                idx = gpr(rn(ins.operands[1]))
            if ins.mnemonic in ('lbzx', 'lhzx') and idx is None and k > 2:
                idx = gpr(rn(ins.operands[2]))
            if ins.mnemonic == 'lis': start = a - 4 * k
        if start is None or idx is None:
            rejects.append(dict(bctr=a, cmp=0, r=-1, labels=[], kind='unguarded', reason='could not identify index/table'))
            continue
        fn, gap = func_of(a)
        labels, clob = [], False
        try:
            for i in range(0, 1024):
                ctr, b, pth, c = simulate(start, idx, i)
                if b != a or ctr & 3 or not (fn[0] <= ctr < fn[1]): break  # stop at first non-code entry
                if i and ctr in (fn[0],): break
                clob |= c
                labels.append(ctr)
        except Fail as e:
            if not labels:
                rejects.append(dict(bctr=a, cmp=0, r=idx, labels=[], kind='unguarded', reason=str(e)))
                continue
        if not labels:
            rejects.append(dict(bctr=a, cmp=0, r=idx, labels=[], kind='unguarded', reason='no in-range entries'))
            continue
        finish(dict(base=start, r=idx, default=0, labels=labels, bctr=a, cmp=0,
                    clobbered=clob, kind='unguarded (size inferred)'), fn, gap)

    with open(out_toml, 'w') as f:
        f.write('# Generated by find_switches.py (XenonAnalyse-compatible)\n')
        for t in sorted(tables.values(), key=lambda t: t['base']):
            f.write(f"[[switch]]\n# {t['kind']}; bctr @ 0x{t['bctr']:X}\nbase = 0x{t['base']:X}\nr = {t['r']}\n"
                    f"default = 0x{t['default']:X}\nlabels = [\n"
                    + ''.join(f'    0x{l:X},\n' for l in t['labels']) + ']\n\n')
    with open(out_funcs, 'w') as f:
        f.write('# Functions not covered by .pdata that contain jump tables\nfunctions = [\n')
        for lo, hi in sorted(gap_funcs):
            f.write(f'    {{ address = 0x{lo:X}, size = 0x{hi - lo:X} }},\n')
        f.write(']\n')
    with open(out_report, 'w') as f:
        inferred = [t for t in tables.values() if t['kind'].startswith('unguarded')]
        f.write(f'# {len(tables)} tables emitted, {len(inferred)} with inferred size, {len(rejects)} rejected\n\n')
        f.write('## Inferred-size tables (verify if behaviour is wrong)\n')
        for t in sorted(inferred, key=lambda t: t['bctr']):
            f.write(f"bctr 0x{t['bctr']:X} r{t['r']} n={len(t['labels'])}\n")
        f.write('\n## Rejected\n')
        for t in sorted(rejects, key=lambda t: t['bctr']):
            f.write(f"bctr 0x{t['bctr']:X} {t['kind']} r{t['r']} n={len(t['labels'])}: {t['reason']}\n")
    gi = sum(1 for t in tables.values() if t['kind'] == 'guarded')
    print(f'tables: {len(tables)} (guarded {gi}, inferred {len(tables) - gi})  '
          f'gap functions: {len(gap_funcs)}  rejected: {len(rejects)}')


if __name__ == '__main__':
    a = sys.argv
    main(a[1], a[2], a[3], a[4], *(int(x, 16) for x in a[5:9]))
