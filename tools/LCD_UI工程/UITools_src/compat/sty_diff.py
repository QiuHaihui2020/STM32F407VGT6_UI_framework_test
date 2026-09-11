# -*- coding: utf-8 -*-
"""两个 .sty 的**结构化**对比：定位到具体是哪个控件的哪个字段不一样。

用法: python sty_diff.py <本次.sty> <参考.sty> [ename.h]
"""
import struct
import sys

from sty_dump import parse
from sty_blocks import PTRS, NONE

U16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
U32 = lambda b, o: struct.unpack_from('<I', b, o)[0]


def recbytes(c):
    return (bytes([c['type'], c['ctrl_num'], c['css_num'], c['len'], c['page'],
                   0xFF, 0xFF, 0xFF])
            + struct.pack('<iI', c['id'], c['css_off']) + bytes.fromhex(c['payload']))


def main():
    pa, pb = sys.argv[1], sys.argv[2]
    ra, rb = open(pa, 'rb').read(), open(pb, 'rb').read()
    A, B = parse(pa), parse(pb)
    names = {}
    if len(sys.argv) > 3:
        import re
        for n, v in re.findall(r'#define\s+(\S+)\s+0X([0-9A-Fa-f]+)',
                               open(sys.argv[3], encoding='utf-8', errors='replace').read()):
            names[int(v, 16) & 0xFFFFFF] = n

    print('文件 %d / %d 字节' % (len(ra), len(rb)))
    ndiff = 0
    for pi, (wa, wb) in enumerate(zip(A['windows'], B['windows'])):
        if (wa['offset'], wa['length'], wa['table_ptr'], wa['table_size']) != \
           (wb['offset'], wb['length'], wb['table_ptr'], wb['table_size']):
            print('页%d 页表项不同: 本次 %s / 参考 %s'
                  % (pi, (wa['offset'], wa['length'], wa['table_ptr'], wa['table_size']),
                     (wb['offset'], wb['length'], wb['table_ptr'], wb['table_size'])))
        # 窗口记录
        fa = wa['controls'][0]['off'] if wa['controls'] else wa['offset']
        fb = wb['controls'][0]['off'] if wb['controls'] else wb['offset']
        if ra[wa['offset']:fa] != rb[wb['offset']:fb]:
            print('页%d 窗口记录不同:\n   本次 %s\n   参考 %s'
                  % (pi, ra[wa['offset']:fa].hex(), rb[wb['offset']:fb].hex()))
        # 控件记录
        for k, (ca, cb) in enumerate(zip(wa['controls'], wb['controls'])):
            xa, xb = recbytes(ca), recbytes(cb)
            if xa == xb:
                continue
            nm = names.get(ca['id'] & 0xFFFFFF, '?')
            bad = [i for i in range(min(len(xa), len(xb))) if xa[i] != xb[i]]
            # 指针字段单独标注（页内偏移不同不算错，只有指向的内容不同才算）
            print('页%d 控件%d(%s type=%d) 字节 %s 不同' % (pi, k, nm, ca['type'], bad))
            print('   本次 %s' % xa.hex())
            print('   参考 %s' % xb.hex())
            ndiff += 1
            if ndiff > 6:
                print('   ...更多略')
                return 1
        # 数据区
        la = wa['controls'][-1]['off'] + wa['controls'][-1]['len'] if wa['controls'] else 0
        lb = wb['controls'][-1]['off'] + wb['controls'][-1]['len'] if wb['controls'] else 0
        da, db = ra[la:wa['table_ptr']], rb[lb:wb['table_ptr']]
        if da != db:
            i = next((i for i in range(min(len(da), len(db))) if da[i] != db[i]), -1)
            print('页%d 数据区 %d/%d 字节，首个不同 @%d(页内 0x%X)'
                  % (pi, len(da), len(db), i, la - wa['offset'] + i))
            print('   本次 %s' % da[max(0, i - 8):i + 24].hex())
            print('   参考 %s' % db[max(0, i - 8):i + 24].hex())
            ndiff += 1
        # 重定位表
        ta = ra[wa['table_ptr']:wa['table_ptr'] + wa['table_size']]
        tb = rb[wb['table_ptr']:wb['table_ptr'] + wb['table_size']]
        if ta != tb:
            i = next((i for i in range(0, min(len(ta), len(tb)), 2)
                      if ta[i:i + 2] != tb[i:i + 2]), -1)
            print('页%d 重定位表 %d/%d 项，首个不同 @%d: 本次 0x%04X 参考 0x%04X'
                  % (pi, len(ta) // 2, len(tb) // 2, i // 2,
                     U16(ta, i), U16(tb, i)))
            ndiff += 1
    print('结构化差异 %d 处' % ndiff)
    return 0 if ndiff == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
