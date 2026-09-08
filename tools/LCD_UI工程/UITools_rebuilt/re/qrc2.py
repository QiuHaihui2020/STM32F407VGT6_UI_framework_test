# -*- coding: utf-8 -*-
"""定点解析一张 qrc 名字表 + 其后的 struct 表 + 其前的 data 表。"""
import struct
import sys
import os
import zlib
from pe import Img

B16 = lambda b, o: struct.unpack_from('>H', b, o)[0]
B32 = lambda b, o: struct.unpack_from('>I', b, o)[0]


def parse_names(buf, start):
    """返回 {相对偏移: 名字}, 表结束位置"""
    names = {}
    j = start
    while j < len(buf) - 6:
        nlen = B16(buf, j)
        if nlen == 0 or nlen > 128:
            break
        body = buf[j + 6:j + 6 + nlen * 2]
        if len(body) < nlen * 2:
            break
        try:
            s = body.decode('utf-16-be')
        except UnicodeDecodeError:
            break
        if not all(0x20 <= ord(c) < 0x7f for c in s):
            break
        names[j - start] = s
        j += 6 + nlen * 2
    return names, j


def parse_data_table(buf, end_hint):
    """data 表从 .rdata 某处开始, 项为 [u32be len][payload]。返回 (base, {off: payload})"""
    # 找第一个合法项：尝试所有 4 字节对齐起点, 要求链条能一路走到 end_hint 附近
    for base in range(0, min(end_hint, 4096), 4):
        p, items, ok = base, {}, True
        while p < end_hint - 4:
            ln = B32(buf, p)
            if ln == 0 or p + 4 + ln > end_hint + 8:
                ok = False
                break
            items[p - base] = buf[p + 4:p + 4 + ln]
            p += 4 + ln
            if p >= end_hint - 8:
                break
        if ok and len(items) >= 4 and abs(p - end_hint) <= 16:
            return base, items
    return None, None


def main():
    img = Img(sys.argv[1])
    name_va = int(sys.argv[2], 16)
    outdir = sys.argv[3]
    buf, base = img.data('.rdata')
    nstart = name_va - base

    names, nend = parse_names(buf, nstart)
    print('名字表: 0x%08x .. 0x%08x  共 %d 项' % (base + nstart, base + nend, len(names)))
    print('  样例:', list(names.values())[:12])

    # 1) 先定位 struct 表：根项 = nameOff 0 / flags 2 / childOff 1
    spos = ent = None
    for cand in range(0, len(buf) - 64, 2):
        if B32(buf, cand) != 0 or B16(buf, cand + 4) != 2 or B32(buf, cand + 10) != 1:
            continue
        cnt = B32(buf, cand + 6)
        if not (1 <= cnt <= 64):
            continue
        for e in (14, 22):
            # 根的第一个孩子必须落在名字表里，且它自己也应是目录（qrc 前缀）
            if names.get(B32(buf, cand + e)) is not None:
                spos, ent = cand, e
                break
        if spos is not None:
            break
    if spos is None:
        print('!! struct 表未定位, nend=0x%08x  后续:' % (base + nend), buf[nend:nend + 64].hex())
        return
    print('struct 表: 0x%08x  entry=%dB  根子项=%d' % (base + spos, ent, B32(buf, spos + 6)))

    # 2) 收集全部文件项的 dataOff，再反解 data 表基址
    raw_files = []
    collect(buf, spos, ent, names, 0, '', raw_files)
    print('结构中文件项 %d 个' % len(raw_files))
    dbase = solve_dbase(buf, [d for _, _, d in raw_files])
    if dbase is None:
        print('!! data 表基址反解失败'); return
    print('数据表 base=0x%08x' % (base + dbase))

    files = []
    for path, flags, doff in raw_files:
        p = dbase + doff
        ln = B32(buf, p)
        files.append((path, flags, buf[p + 4:p + 4 + ln]))
    dump(files, outdir)


def collect(buf, spos, ent, names, idx, prefix, out, depth=0):
    off = spos + idx * ent
    if depth > 12 or off + ent > len(buf):
        return
    nm = names.get(B32(buf, off))
    if nm is None:
        return
    flags = B16(buf, off + 4)
    if flags & 0x02:
        cnt, child = B32(buf, off + 6), B32(buf, off + 10)
        path = (prefix + '/' + nm) if prefix else nm
        for k in range(cnt):
            collect(buf, spos, ent, names, child + k, path, out, depth + 1)
    else:
        out.append(((prefix + '/' + nm) if prefix else nm, flags, B32(buf, off + 10)))


def solve_dbase(buf, doffs):
    """用 PNG 魔数锚点反解 data 表基址：某处 [u32be len][\\x89PNG]"""
    from collections import Counter
    anchors = []
    st = 0
    while True:
        p = buf.find(b'\x89PNG\r\n\x1a\n', st)
        if p < 0:
            break
        st = p + 1
        if p >= 4 and B32(buf, p - 4) > 40:
            anchors.append(p - 4)
    votes = Counter()
    for a in anchors:
        for d in doffs:
            if a - d >= 0:
                votes[a - d] += 1
    if not votes:
        return None
    cand, n = votes.most_common(1)[0]
    print('  基址投票: 0x%x 命中 %d/%d' % (cand, n, len(doffs)))
    return cand if n >= 2 else None


def walk(buf, spos, ent, names, ditems, idx, prefix, out, depth=0):
    off = spos + idx * ent
    if depth > 12:
        return
    noff = B32(buf, off)
    flags = B16(buf, off + 4)
    nm = names.get(noff)
    if nm is None:
        return
    if flags & 0x02:
        cnt = B32(buf, off + 6)
        child = B32(buf, off + 10)
        path = (prefix + '/' + nm) if prefix else nm
        for k in range(cnt):
            walk(buf, spos, ent, names, ditems, child + k, path, out, depth + 1)
    else:
        doff = B32(buf, off + 10)
        if doff in ditems:
            out.append(((prefix + '/' + nm) if prefix else nm, flags, ditems[doff]))


def dump(files, outdir):
    for path, flags, payload in files:
        if flags & 0x01:
            try:
                payload = zlib.decompress(payload[4:])
            except Exception:
                pass
        fp = os.path.join(outdir, path.replace('/', os.sep))
        os.makedirs(os.path.dirname(fp) or '.', exist_ok=True)
        open(fp, 'wb').write(payload)
        print('  %-46s %6d B' % (path, len(payload)))
    print('共导出 %d 个资源 -> %s' % (len(files), outdir))


if __name__ == '__main__':
    main()
