# -*- coding: utf-8 -*-
"""qrc 还原 v3：靠"连续多项 nameOff 命中名字表"来锁定 struct 表。"""
import struct
import sys
import os
import zlib
from pe import Img
from qrc2 import parse_names

B16 = lambda b, o: struct.unpack_from('>H', b, o)[0]
B32 = lambda b, o: struct.unpack_from('>I', b, o)[0]


def find_struct(buf, names, lo, hi):
    best = None
    for ent in (14, 22):
        pos = lo
        while pos < hi - ent * 4:
            k = 0
            while pos + (k + 1) * ent <= len(buf):
                noff = B32(buf, pos + k * ent)
                fl = B16(buf, pos + k * ent + 4)
                if noff not in names or (fl & ~0x0003):
                    break
                k += 1
            if k >= 8 and (best is None or k > best[2]):
                best = (pos, ent, k)
            pos += (k * ent) if k else 2
    return best


def main():
    img = Img(sys.argv[1])
    name_va = int(sys.argv[2], 16)
    outdir = sys.argv[3]
    buf, base = img.data('.rdata')
    names, nend = parse_names(buf, name_va - base)
    nstart = name_va - base
    print('名字表 0x%08x..0x%08x  %d 项' % (base + nstart, base + nend, len(names)))

    lo = max(0, nstart - 0x20000)
    hi = min(len(buf), nend + 0x20000)
    got = find_struct(buf, names, lo, hi)
    if not got:
        print('!! struct 表仍未定位'); return
    spos, ent, k = got
    # 命中区是文件项，真正的表头（根目录）在更前面：根 = noff 0 / flags 2 / firstChild 1
    for back in range(1, 10):
        o = spos - back * ent
        if o < 0:
            break
        if B32(buf, o) == 0 and B16(buf, o + 4) == 2 and B32(buf, o + 10) == 1:
            spos, k = o, k + back
            break
    print('struct 表 0x%08x  entry=%dB  共 %d 项' % (base + spos, ent, k))

    entries = []
    for i in range(k):
        o = spos + i * ent
        entries.append((B32(buf, o), B16(buf, o + 4), B32(buf, o + 6), B32(buf, o + 10)))
    for i, (noff, fl, a, b2) in enumerate(entries[:12]):
        print('  %3d %s %-24r a=%-8d b=%d' % (i, 'DIR ' if fl & 2 else 'FILE',
                                              names[noff], a, b2))

    files = []
    collect(entries, names, 0, '', files)
    print('文件项 %d 个' % len(files))
    dbase = solve_dbase(buf, [d for _, _, d in files])
    if dbase is None:
        print('!! data 基址反解失败'); return
    print('data 表 base=0x%08x' % (base + dbase))
    dump(buf, dbase, files, outdir)


def collect(entries, names, idx, prefix, out, depth=0):
    if idx >= len(entries) or depth > 12:
        return
    noff, fl, a, b2 = entries[idx]
    nm = '' if depth == 0 else names.get(noff)   # 根项名字为空，其 noff=0 会误命中首个名字
    if nm is None:
        return
    if fl & 0x02:
        path = (prefix + '/' + nm) if prefix else nm
        for kk in range(a):
            collect(entries, names, b2 + kk, path, out, depth + 1)
    else:
        out.append(((prefix + '/' + nm) if prefix else nm, fl, b2))


def solve_dbase(buf, doffs):
    from collections import Counter
    anchors = []
    st = 0
    while True:
        p = buf.find(b'\x89PNG\r\n\x1a\n', st)
        if p < 0:
            break
        st = p + 1
        if p >= 4 and 40 < B32(buf, p - 4) < 4 * 1024 * 1024:
            anchors.append(p - 4)
    votes = Counter()
    ds = set(doffs)
    for a in anchors:
        for d in ds:
            if a - d >= 0:
                votes[a - d] += 1
    if not votes:
        return None
    cand, n = votes.most_common(1)[0]
    print('  基址投票 0x%x 命中 %d/%d' % (cand, n, len(ds)))
    return cand if n >= 2 else None


def dump(buf, dbase, files, outdir):
    ok = 0
    for path, fl, doff in files:
        p = dbase + doff
        ln = B32(buf, p)
        if ln == 0 or p + 4 + ln > len(buf):
            print('  ?? %-44s 长度异常 %d' % (path, ln))
            continue
        payload = buf[p + 4:p + 4 + ln]
        if fl & 0x01:
            try:
                payload = zlib.decompress(payload[4:])
            except Exception:
                pass
        fp = os.path.join(outdir, path.replace('/', os.sep))
        os.makedirs(os.path.dirname(fp) or '.', exist_ok=True)
        open(fp, 'wb').write(payload)
        ok += 1
        print('  %-46s %7d B  %s' % (path, len(payload), payload[:4].hex()))
    print('导出 %d/%d -> %s' % (ok, len(files), outdir))


if __name__ == '__main__':
    main()
