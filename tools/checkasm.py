#!/usr/bin/env python3
"""
Guards against an SDCC 4.5.0 z80 code generation bug.

For code like

    void f(u8 n) { if (g != n) { g = n; ext(); } }

SDCC emits

    ld   hl,#_g
    sub  a,(hl)        ; <- destroys A, which still held `n`
    ret  Z
    ld   (_g+0),a      ; <- stores (n - g) instead of n

i.e. it uses the destructive `sub` where `cp` was meant and then keeps using A
as if it were unchanged.  The bug is present with and without --peep-file and
at every optimisation level (it disappears with --sdcccall 0, which we cannot
use because the bundled SMSlib.lib is an __sdcccall(1) build).

This script scans the generated assembly for the signature: an instruction
that destroys A, followed - across conditional branches only - by a read of A
with no intervening redefinition.  Any hit is a real miscompile and has to be
worked around in the C source (introducing a local temporary makes SDCC emit
`cp` instead).

Usage:  python3 tools/checkasm.py build/*.asm
"""
import re
import sys

# The miscompile always looks the same: a *destructive* compare (`sub`, or the
# `sbc` half of a 16 bit compare) whose result is immediately branched on, and
# then A read back as if it still held the left hand operand.  Non destructive
# compares (`cp`), flag tests (`or a,a`) and ordinary arithmetic whose result
# really is wanted are not matched.
DEST = re.compile(r'^\s*(sub|sbc)\s+a\s*,\s*(.+?)\s*$', re.I)
SETS_A = re.compile(r'^\s*(ld\s+a\s*,|pop\s+af|in\s+a\s*,|ex\s+af|rlca|rrca|rla|rra|cpl|neg|daa'
                    r'|sub|add|adc|sbc|xor|or|and)', re.I)
READS_A = re.compile(r'^\s*(ld\s+\(.*\)\s*,\s*a|ld\s+[bcdehl]\s*,\s*a|ld\s+i[xy][hl]\s*,\s*a'
                     r'|push\s+af|out\s+\(.*\)\s*,\s*a|ld\s+[0-9-]+\s*\(i[xy]\)\s*,\s*a)', re.I)
CONDBR = re.compile(r'^\s*(ret|jr|jp|call)\s+(nz|z|nc|c|po|pe|p|m)\b', re.I)
LABEL = re.compile(r'^\S+:')


def code(line):
    return line.split(';')[0]


def skippable(line):
    t = code(line)
    return (not t.strip()) or LABEL.match(line) or t.strip().startswith('.')


def scan(path):
    hits = []
    lines = open(path, errors='replace').read().split('\n')
    for i, line in enumerate(lines):
        m = DEST.match(code(line))
        if not m or m.group(2).lower().strip() == 'a':
            continue
        j = i + 1
        while j < len(lines) and skippable(lines[j]):
            j += 1
        if j >= len(lines) or not CONDBR.match(code(lines[j])):
            continue
        for k in range(j + 1, min(j + 6, len(lines))):
            t = code(lines[k])
            if skippable(lines[k]):
                continue
            if READS_A.match(t):
                ctx = ''
                for q in range(i, max(0, i - 10), -1):
                    if lines[q].startswith(';') and '.c:' in lines[q]:
                        ctx = lines[q].strip()
                        break
                hits.append((path, i + 1, code(line).strip(), t.strip(), ctx))
                break
            if SETS_A.match(t):
                break
            if CONDBR.match(t):
                continue
            break
    return hits


def main():
    total = []
    for p in sys.argv[1:]:
        total += scan(p)
    for path, ln, bad, use, ctx in total:
        print('%s:%d: MISCOMPILE - A destroyed by "%s" then read by "%s"' % (path, ln, bad, use))
        if ctx:
            print('    %s' % ctx)
    if total:
        print('--- %d miscompile(s); introduce a local temporary so SDCC emits `cp` ---'
              % len(total))
    else:
        print('checkasm: clean')
    return 1 if total else 0


sys.exit(main())
