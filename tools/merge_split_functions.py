#!/usr/bin/env python3
"""
Fix functions that XenonRecomp's analyser split too early.

Reads the generated C++ for `// ERROR <addr>` markers (a branch whose target is
inside another function, not at its start), works out the source function and
the function that contains the target, and emits a `functions = [...]` list in
which each such pair is merged into one span. Existing explicit entries are
kept and unioned with the new spans. Run, recompile, repeat until no markers
remain or no new merges are found.

usage: merge_split_functions.py <ppc_dir> <config.toml> [max_span_hex]
Rewrites the `functions = [` block at the end of the config in place.
"""
import re, sys, glob, bisect

ppc_dir, cfg_path = sys.argv[1], sys.argv[2]
max_span = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x8000

impl = re.compile(r'^PPC_FUNC_IMPL\(__imp__sub_([0-9A-F]+)\)')
err = re.compile(r'// ERROR ([0-9A-F]+)')

bases, errors = set(), []
for path in glob.glob(f'{ppc_dir}/ppc_recomp.*.cpp'):
    cur = None
    for line in open(path):
        m = impl.match(line)
        if m:
            cur = int(m.group(1), 16); bases.add(cur); continue
        m = err.search(line)
        if m and cur is not None:
            errors.append((cur, int(m.group(1), 16)))
bases = sorted(bases)

def containing(a):
    i = bisect.bisect_right(bases, a) - 1
    return bases[i], (bases[i + 1] if i + 1 < len(bases) else a + 4)

def end_of(base):
    i = bisect.bisect_right(bases, base) - 1
    return bases[i + 1] if i + 1 < len(bases) else base + 4

cfg = open(cfg_path).read()
head, _, tail = cfg.partition('functions = [')
existing = [(int(a, 16), int(a, 16) + int(s, 16))
            for a, s in re.findall(r'address = 0x([0-9A-F]+), size = 0x([0-9A-F]+)', tail)]

spans, skipped = list(existing), []
for src, tgt in errors:
    tb, te = containing(tgt)
    lo, hi = min(src, tb), max(end_of(src), te)
    if hi - lo > max_span:
        skipped.append((src, tgt, hi - lo)); continue
    spans.append((lo, hi))

# union overlapping spans
spans.sort()
merged = []
for lo, hi in spans:
    if merged and lo < merged[-1][1]:
        merged[-1] = (merged[-1][0], max(merged[-1][1], hi))
    else:
        merged.append((lo, hi))

added = len(merged) - len(set(existing))
with open(cfg_path, 'w') as f:
    f.write(head.rstrip() + '\n\n')
    f.write('# Function boundaries: .pdata gaps holding jump tables, and spans merged\n'
            '# where the analyser ended a function before code that branches back into it\n'
            'functions = [\n')
    for lo, hi in merged:
        f.write(f'    {{ address = 0x{lo:X}, size = 0x{hi - lo:X} }},\n')
    f.write(']\n')
print(f'errors: {len(errors)}  functions now: {len(merged)} (was {len(existing)})  skipped (too large): {len(skipped)}')
for s in skipped[:10]:
    print(f'  skipped src 0x{s[0]:X} -> 0x{s[1]:X} span 0x{s[2]:X}')
