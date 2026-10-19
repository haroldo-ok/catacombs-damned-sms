#!/usr/bin/env python3
"""prof3.py build/cg.out catacombs.noi build/*.rst [--top N] [--asm FUNC]

Same job as devkitSMS' prof2.py, but this SDCC writes eight hex digits in the
address column of its .rst listings and prof2.py's label regex only accepts
six - so every static function was being folded into the previous exported
symbol ("DrawCells" was really DrawCells + ResampleWord + EnsureColumn +
BlendColumn).  This one reads the listings properly, so statics, inline-asm
labels and library helpers all show up under their own name.
"""
import sys, re, bisect, collections

args = sys.argv[1:]
opt = {}
for k in ('--top', '--asm', '--callers'):
    if k in args:
        i = args.index(k); opt[k] = args[i + 1]; del args[i:i + 2]
if len(args) < 2:
    print(__doc__); sys.exit(2)

dump, noi, rsts = args[0], args[1], args[2:]
syms = {}

for l in open(noi):
    m = re.match(r'DEF\s+(\S+)\s+0x([0-9A-Fa-f]+)', l)
    if m:
        a = int(m.group(2), 16); n = m.group(1)
        if a >= 0x10000 and 0x8000 <= (a & 0xFFFF) < 0xC000:
            a &= 0xFFFF
        if a < 0xC000 and not n.startswith(('l_', 's_', '.')) and '$' not in n:
            syms.setdefault(a, n.lstrip('_'))

lab = re.compile(r'^\s+([0-9A-F]{4,8})\s+\d+\s+([A-Za-z_][A-Za-z0-9_]*)::?\s*$')
ins = re.compile(r'^\s+([0-9A-F]{4,8})\s+([0-9A-F ]{2,12}?)\s+\[\s*\d+\]\s+\d+\s+(.*)$')
lines = {}
for r in rsts:
    for l in open(r, errors='replace'):
        m = lab.match(l)
        if m:
            a = int(m.group(1), 16)
            if a >= 0x10000 and 0x8000 <= (a & 0xFFFF) < 0xC000:
                a &= 0xFFFF
            if a < 0xC000:
                syms[a] = m.group(2).lstrip('_')
        m = ins.match(l)
        if m:
            a = int(m.group(1), 16)
            if a >= 0x10000 and 0x8000 <= (a & 0xFFFF) < 0xC000:
                a &= 0xFFFF
            lines[a] = m.group(3).strip()

addrs = sorted(syms); names = [syms[a] for a in addrs]


def owner(a):
    if a == 0x38: return 'IRQ'
    if a == 0x66: return 'NMI'
    if a == 0xFFFFFF: return 'ROOT'
    i = bisect.bisect_right(addrs, a) - 1
    return names[i] if i >= 0 else '?%06X' % a


T = 0
ex = collections.Counter(); per = {}; inc = []; edg = []
for l in open(dump):
    p = l.split()
    if p[0] == 'T':
        T = int(p[1])
    elif p[0] == 'X':
        a = int(p[1], 16); c = int(p[2]); ex[owner(a)] += c; per[a] = c
    elif p[0] == 'I':
        inc.append((int(p[3]), int(p[2]), owner(int(p[1], 16))))
    elif p[0] == 'E':
        edg.append((int(p[1], 16), int(p[2], 16), int(p[3]), int(p[4])))

if '--callers' in opt:
    want = opt['--callers']
    agg = collections.defaultdict(lambda: [0, 0])
    for a, b, n, c in edg:
        if owner(b) == want:
            e = agg[owner(a)]; e[0] += n; e[1] += c
    print('callers of %s' % want)
    for n, (calls, cyc) in sorted(agg.items(), key=lambda kv: -kv[1][1]):
        print('  %-26s calls %7d   %10d cycles  %6.0f/call' %
              (n, calls, cyc, cyc / max(calls, 1)))
    sys.exit(0)

top = int(opt.get('--top', 25))
print('total %d cycles' % T)
print('\n== exclusive (self) ==')
for n, c in ex.most_common(top):
    print('%-26s %10d %5.1f%%' % (n, c, 100.0 * c / T))
print('\n== inclusive ==')
for c, calls, n in sorted(inc, reverse=True)[:top]:
    print('%-26s %10d %5.1f%%  calls %6d  %8.0f cyc/call'
          % (n, c, 100.0 * c / T, calls, c / max(calls, 1)))

if '--asm' in opt:
    f = opt['--asm']
    cand = [a for a in addrs if syms[a] == f]
    if not cand:
        print('no such symbol'); sys.exit(1)
    a0 = cand[0]; a1 = addrs[addrs.index(a0) + 1]
    print('\n== %s per instruction (%d cycles total) ==' %
          (f, sum(per.get(a, 0) for a in range(a0, a1))))
    for a in range(a0, a1):
        if a in lines:
            print('%06X %10d  %s' % (a, per.get(a, 0), lines[a]))
