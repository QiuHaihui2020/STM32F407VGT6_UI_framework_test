# -*- coding: utf-8 -*-
"""`.sty` 生成器 —— 从语义模型重新排布整个文件。

和 sty_roundtrip.py 的区别很关键：
  - roundtrip 是"把字节切开再拼回去"，只能证明切分没漏；
  - 本文件是"把偏移全部丢掉，只留下语义（每个控件有哪些 css / 列表 / 动作），
    然后自己重新分配地址、重算所有指针、重建页尾重定位表"。
    如果重排后仍与原文件逐字节相同，说明排布算法也复现对了 —— 这才是能写
    QtToolBin 的证据。

排布规律（实测反推）：
  数据区从**高地址往低地址**分配，控件按顺序各占一"组"；
  组内字段顺序固定，每块按 4 字节对齐、尾部用 0xFF 填充；
  空列表照样写一个 0x0000。

用法: python sty_gen.py <JL.sty>
"""
import struct
import sys
from sty_dump import parse
from sty_blocks import PTRS, CSS_SIZE, NONE

U16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
U32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
HEAD_SZ, WHEAD_SZ, CHEAD_SZ = 24, 20, 16


def align4(n):
    return (n + 3) & ~3


def crc16_xmodem(data, crc=0):
    """固件 liba/common/jl_crc.c 里的 CRC16() 就是它（init=0）。"""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def read_block(raw, addr, kind, hi):
    """把一个数据块读成"值"，不带地址。"""
    if kind == 'css':
        return ('css', raw[addr:addr + CSS_SIZE])
    if kind in ('img_normal', 'img_high', 'img_charge'):
        n = U16(raw, addr)
        return ('list', raw[addr:addr + 2 + 2 * n])
    if kind == 'strlist':
        n = U16(raw, addr)
        return ('list', raw[addr:addr + 2 + 2 * n])
    if kind == 'action':
        n = U16(raw, addr)
        size, p = 2, addr + 2
        for _ in range(n):
            if p + 9 > hi:
                break
            step = align4(9 + raw[p + 8])
            size += step
            p = addr + size
        return ('action', raw[addr:addr + size])
    return None


def build_model(path):
    raw = open(path, 'rb').read()
    doc = parse(path)
    model = {'head': doc['head'], 'raw': raw, 'pages': []}
    for w in doc['windows']:
        base, lo, hi = w['offset'], w['scan_stopped_at'], w['table_ptr']
        pg = {'window_record': raw[base:w['controls'][0]['off']] if w['controls'] else b'',
              'controls': [], 'crc': w['crc']}
        for c in w['controls']:
            pl = bytes.fromhex(c['payload'])
            rec = bytearray(bytes([c['type'], c['ctrl_num'], c['css_num'], c['len'],
                                   c['page'], 0xFF, 0xFF, 0xFF])
                            + struct.pack('<iI', c['id'], c['css_off']) + pl)
            fields = []            # [(字段偏移, 名字, 数据, 是否要回填指针)]
            raws = []              # 指向控件区的指针原样保留
            for name, off in PTRS.get(c['type'], [('css', 12)]):
                if off + 4 > len(rec):
                    continue
                pv = U32(rec, off)
                if name in ('ctrl', 'layout', 'info'):
                    if pv not in NONE:
                        raws.append((off, pv))     # 指向控件记录，原样保留
                    continue
                if pv in NONE:
                    # 列表字段即使指针为空，**照样要写一个 0x0000 空块**
                    # （页 0 有 12 个、页 1 有 25 个、页 2 有 22 个，合计 59 个，
                    #   正好补齐早先少的 236 字节）。指针保持 null，不回填。
                    if name.startswith('img') or name == 'strlist':
                        fields.append((off, name, b'\x00\x00', False))
                    continue
                blk = read_block(raw, base + (pv & 0xFFFF), name, hi)
                if blk:
                    fields.append((off, name, blk[1], True))
            pg['controls'].append({'off': c['off'], 'rec': rec, 'fields': fields,
                                   'rawptrs': raws, 'len': c['len']})
        # 组内块的实际顺序：按地址从高到低（数据区是倒着长的）
        pg['data_lo'], pg['data_hi'] = lo, hi
        pg['table'] = raw[hi:hi + w['table_size']]
        # 窗口记录里那个指向第一个控件的指针，在页内偏移 0x18（实测三页一致）
        pg['win_ptr_off'] = len(pg['window_record']) - 4
        model['pages'].append(pg)
    return model


def emit(model, order_desc=True):
    """按模型重新生成整个文件。"""
    raw = model['raw']
    h = model['head']
    pages = model['pages']
    npg = len(pages)

    # 1) 先按控件长度算出控件区，得到每个控件的新页内偏移
    page_bodies = []
    for pg in pages:
        body = bytearray(pg['window_record'])
        newoff = {}
        for c in pg['controls']:
            newoff[c['off']] = len(body)
            body += c['rec']
        pg['newoff'] = newoff
        page_bodies.append(body)

    # 2) 数据区：倒着分配。先算总长，再从高到低摆
    for pi, pg in enumerate(pages):
        body = page_bodies[pi]
        ctrl_end = len(body)
        # 收集所有要摆的块，顺序 = 控件顺序 x 组内字段顺序（按原文件里地址降序）
        # 排布规律（order2.py 从原文件统计出来的）：
        #   组内：按结构体字段偏移**升序**摆（css 在最低，action 在最高）
        #   组间：控件 0 的组在**最高地址**，控件 N 的在最低 —— 整体倒着长
        #   每块 4 字节对齐、尾部 0xFF 填充；空列表也占一个 0x0000
        groups = []
        total = 0
        for c in pg['controls']:
            flds = sorted(c['fields'], key=lambda f: f[0])      # 按字段偏移升序
            gsz = sum(align4(len(d)) for _, _, d, _ in flds)
            groups.append((c, flds, gsz))
            total += gsz

        placed = []
        pos = total
        for c, flds, gsz in groups:            # 控件 0 先摆，占最高处
            pos -= gsz
            p = pos
            for off, name, blk, patch in flds:
                placed.append((p, blk, align4(len(blk)), c, off, patch))
                p += align4(len(blk))
        assert pos == 0, pos
        buf = bytearray(b'\xff' * total)
        for p, blk, sz, c, off, patch in placed:
            buf[p:p + len(blk)] = blk
        pg['data'] = buf
        pg['ctrl_end'] = ctrl_end
        pg['placed'] = placed

    # 3) 组装：文件头 + 页表 + 各页(窗口记录+控件+数据+索引表)
    out = bytearray()
    out += struct.pack('<IIII', h['ui_version'], h['magic2'], h['hdr_ptr'], h['total_size'])
    out += struct.pack('<BBHB', h['type'], npg, h['prop_len'], h['rotate'])
    out += bytes.fromhex(h['rev'])
    wheads = []
    cur = HEAD_SZ + WHEAD_SZ * npg
    for pi, pg in enumerate(pages):
        body = page_bodies[pi]
        dlen = len(pg['data'])
        tlen = len(pg['table'])
        offset = cur
        length = len(body) + dlen + tlen
        tptr = offset + len(body) + dlen
        wheads.append((offset, length, tptr, tlen, pg['crc']))
        cur += length
        pg['offset'] = offset
        pg['tptr'] = tptr
    # 页表项里的三个 CRC 自己算（字段名取自固件 struct window_head）：
    #   crc_data  = CRC16(页数据 [offset, offset+len))
    #   crc_table = CRC16(指针表)
    #   crc_head  = CRC16(本表项前 14 字节)
    # 固件对 crc_head / crc_data 是真校验，对不上直接返回 NULL。
    wh_bytes = []
    for i, (offset, length, tptr, tlen, _crc) in enumerate(wheads):
        pg = pages[i]
        body = bytearray(page_bodies[i])
        page_data = bytes(body) + bytes(pg['data']) + bytes(pg['table'])
        # 注意：指针还没回填，这里只占位；真正的 CRC 在下面回填完再算
        wh_bytes.append([offset, length, tptr, tlen])

    # 3.5) 重建页尾的重定位表
    #      规律（对着原文件核过）：控件**从最后一个往前**遍历，
    #      每个控件内部按字段偏移升序，最后补一项窗口记录里的指针偏移。
    for pg in pages:
        ent = []
        for c in reversed(pg['controls']):
            recoff = pg['newoff'][c['off']]
            offs = sorted([f[0] for f in c['fields']] + [o for o, _ in c['rawptrs']])
            for o in offs:
                ent.append(recoff + o)
        ent.append(pg['win_ptr_off'])
        pg['table_new'] = struct.pack('<%dH' % len(ent), *ent)
        if pg['table_new'] != pg['table']:
            import sys as _s
            print('  ! 重定位表不一致: 原 %d 项 / 重建 %d 项' %
                  (len(pg['table']) // 2, len(ent)), file=_s.stderr)
            a = pg['table']; b = pg['table_new']
            for i in range(min(len(a), len(b)) // 2):
                if a[2 * i:2 * i + 2] != b[2 * i:2 * i + 2]:
                    print('    首个不同 @项%d: 原 0x%04X 重建 0x%04X' %
                          (i, struct.unpack_from('<H', a, 2 * i)[0],
                           struct.unpack_from('<H', b, 2 * i)[0]), file=_s.stderr)
                    break
            pg['reloc_ok'] = False
        else:
            pg['table'] = pg['table_new']
            pg['reloc_ok'] = True

    # 4) 回填指针 -> 生成每页字节 -> 算 CRC -> 拼装
    page_bytes = []
    for pi, pg in enumerate(pages):
        body = bytearray(page_bodies[pi])
        dstart = len(body)                     # 数据区在页内的偏移
        for p, blk, sz, c, off, patch in pg['placed']:
            if not patch:
                continue                       # 空列表：块要写，指针保持 null
            struct.pack_into('<I', body, pg['newoff'][c['off']] + off, dstart + p)
        for c in pg['controls']:
            for off, oldp in c['rawptrs']:
                # 指向控件记录的指针：控件顺序没变，原值可直接沿用
                struct.pack_into('<I', body, pg['newoff'][c['off']] + off, oldp)
        page_bytes.append(bytes(body) + bytes(pg['data']) + bytes(pg['table']))

    for i, (offset, length, tptr, tlen) in enumerate(wh_bytes):
        pg = pages[i]
        c_data = crc16_xmodem(page_bytes[i])
        c_table = crc16_xmodem(bytes(pg['table']))
        hdr14 = struct.pack('<IIIH', offset, length, tptr, tlen)
        c_head = crc16_xmodem(hdr14)
        out += hdr14 + struct.pack('<HHH', c_data, c_table, c_head)
    for b in page_bytes:
        out += b
    return bytes(out)


def main():
    path = sys.argv[1]
    orig = open(path, 'rb').read()
    m = build_model(path)
    gen = emit(m)
    nok = sum(1 for pg in m['pages'] if pg.get('reloc_ok'))
    print('重定位表重建一致: %d/%d 页' % (nok, len(m['pages'])))
    print('原文件 %d 字节，重排后 %d 字节' % (len(orig), len(gen)))
    if gen == orig:
        print('★ 逐字节相同 —— 排布算法复现成功')
        return 0
    n = min(len(orig), len(gen))
    for i in range(n):
        if orig[i] != gen[i]:
            a = max(0, i - 16)
            print('首个差异 @0x%X' % i)
            print('  原  : %s' % orig[a:i + 24].hex())
            print('  重排: %s' % gen[a:i + 24].hex())
            break
    return 1


if __name__ == '__main__':
    sys.exit(main())
