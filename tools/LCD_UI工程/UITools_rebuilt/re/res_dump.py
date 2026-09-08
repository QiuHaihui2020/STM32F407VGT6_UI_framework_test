# -*- coding: utf-8 -*-
"""解析 JL.res / JL.str（原厂 result.bin / result.str），并统计字节覆盖率。

结构取自固件 User/ui_framework/liba/res/resfile.c 的**实际读取顺序**
（注意不是照结构体声明顺序猜的）：

    0x00  RES_HEAD_T  16 B
          u8 magic[4]="RU21"; u16 version; u16 bPanelType;
          u16 totalPage; u16 reserved; u32 resver
    0x10  RES_PAGE_T[totalPage]  8 B/项  { u32 pageNum; u32 pageAddr; }

    每页 pageAddr 处有**两个** RES_ENTRY_T（12 B/个）：
        pageAddr + 0   调色板表   bItemType = 0x54
        pageAddr + 12  图片/字符串表  bItemType = 0x50   <- 固件读的是这个
      RES_ENTRY_T { u32 dwOffset; u16 wCount; u8 bItemType; u8 langsum; u32 language; }

    调色板表项 RES_PAL_T 12 B { u32 num; u32 dwOffset; u32 dwLength; }
    图片表项   RES_BMP_T 20 B
          u16 head_crc; u16 data_crc; u16 res_type; u16 typeId;
          u16 wWidth;   u16 wHeight;  u32 dwLength; u32 dwOffset;

    typeId 是打包的：
        format   = (typeId >> 10) & 0x07
        compress = (typeId >> 13)
        id       = typeId & 0x3FF        <- 与 result_pic_index.h 里的编号对应
    head_crc = CRC16(从 data_crc 起的 18 字节)      （固件会校验）
    资源 id 从 1 开始：第 i 项在 dwOffset + (id-1)*20

用法: python res_dump.py <JL.res>
"""
import struct
import sys

U8 = lambda b, o: b[o]
U16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
U32 = lambda b, o: struct.unpack_from('<I', b, o)[0]

HEAD_SZ, PAGE_SZ, ENTRY_SZ, BMP_SZ, PAL_SZ = 16, 8, 12, 20, 12
PIXEL_FMT = {0: 'ARGB8888', 1: 'RGB888', 2: 'RGB565', 3: 'L8', 4: 'AL88', 5: 'AL44',
             6: 'A8', 7: 'L1'}


def crc16_xmodem(data, crc=0):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def parse(path):
    raw = open(path, 'rb').read()
    d = {'raw': raw, 'size': len(raw), 'magic': raw[0:4], 'version': U16(raw, 4),
         'panel': U16(raw, 6), 'total_page': U16(raw, 8), 'reserved': U16(raw, 10),
         'resver': U32(raw, 12), 'pages': []}

    # .str 没有页表：头后面直接跟一个 RES_ENTRY_T（bItemType='S'=0x53），
    # wCount 是所有语言的条目总数，langsum 是语言数，language 是语言位掩码。
    # 固件 open_string_pic 就是 fseek(sizeof(RES_HEAD_T)) 直接读 entry。
    d['is_str'] = raw[HEAD_SZ + 6] == 0x53
    if d['is_str']:
        e = {'at': HEAD_SZ, 'dwOffset': U32(raw, HEAD_SZ), 'wCount': U16(raw, HEAD_SZ + 4),
             'bItemType': U8(raw, HEAD_SZ + 6), 'langsum': U8(raw, HEAD_SZ + 7),
             'language': U32(raw, HEAD_SZ + 8)}
        pg = {'num': 0, 'addr': HEAD_SZ, 'pal_entry': None, 'item_entry': e,
              'palettes': [], 'items': []}
        for k in range(e['wCount']):
            o2 = e['dwOffset'] + k * BMP_SZ
            if o2 + BMP_SZ > len(raw):
                break
            tid = U16(raw, o2 + 6)
            it = {'at': o2, 'head_crc': U16(raw, o2), 'data_crc': U16(raw, o2 + 2),
                  'res_type': U16(raw, o2 + 4), 'typeId': tid,
                  'fmt': (tid >> 10) & 7, 'compress': tid >> 13, 'id': tid & 0x3FF,
                  'w': U16(raw, o2 + 8), 'h': U16(raw, o2 + 10),
                  'len': U32(raw, o2 + 12), 'off': U32(raw, o2 + 16)}
            it['crc_ok'] = crc16_xmodem(raw[o2 + 2:o2 + BMP_SZ]) == it['head_crc']
            pg['items'].append(it)
        d['pages'].append(pg)
        return d

    for i in range(d['total_page']):
        o = HEAD_SZ + i * PAGE_SZ
        p = {'num': U32(raw, o), 'addr': U32(raw, o + 4)}
        a = p['addr']

        def rd_entry(at):
            return {'at': at, 'dwOffset': U32(raw, at), 'wCount': U16(raw, at + 4),
                    'bItemType': U8(raw, at + 6), 'langsum': U8(raw, at + 7),
                    'language': U32(raw, at + 8)}
        p['pal_entry'] = rd_entry(a)
        p['item_entry'] = rd_entry(a + ENTRY_SZ)

        p['palettes'] = []
        for k in range(p['pal_entry']['wCount']):
            o2 = p['pal_entry']['dwOffset'] + k * PAL_SZ
            p['palettes'].append({'at': o2, 'num': U32(raw, o2),
                                  'off': U32(raw, o2 + 4), 'len': U32(raw, o2 + 8)})

        p['items'] = []
        for k in range(p['item_entry']['wCount']):
            o2 = p['item_entry']['dwOffset'] + k * BMP_SZ
            if o2 + BMP_SZ > len(raw):
                break
            tid = U16(raw, o2 + 6)
            it = {'at': o2, 'head_crc': U16(raw, o2), 'data_crc': U16(raw, o2 + 2),
                  'res_type': U16(raw, o2 + 4), 'typeId': tid,
                  'fmt': (tid >> 10) & 7, 'compress': tid >> 13, 'id': tid & 0x3FF,
                  'w': U16(raw, o2 + 8), 'h': U16(raw, o2 + 10),
                  'len': U32(raw, o2 + 12), 'off': U32(raw, o2 + 16)}
            it['crc_ok'] = crc16_xmodem(raw[o2 + 2:o2 + BMP_SZ]) == it['head_crc']
            p['items'].append(it)
        d['pages'].append(p)
    return d


def coverage(d):
    raw = d['raw']
    cov = bytearray(len(raw))

    def mark(o, n):
        for i in range(max(0, o), min(o + n, len(raw))):
            cov[i] = 1
    mark(0, HEAD_SZ)
    if not d.get('is_str'):
        mark(HEAD_SZ, PAGE_SZ * d['total_page'])
    for p in d['pages']:
        mark(p['addr'], ENTRY_SZ * (1 if d.get('is_str') else 2))
        if p['pal_entry']:
            mark(p['pal_entry']['dwOffset'], PAL_SZ * len(p['palettes']))
        for q in p['palettes']:
            mark(q['off'], q['len'])
        mark(p['item_entry']['dwOffset'], BMP_SZ * len(p['items']))
        for it in p['items']:
            mark(it['off'], it['len'])
    used = sum(cov)
    runs, i = [], 0
    while i < len(cov):
        if not cov[i]:
            j = i
            while j < len(cov) and not cov[j]:
                j += 1
            runs.append((i, j - i))
            i = j
        else:
            i += 1
    return used, runs


def main():
    path = sys.argv[1]
    d = parse(path)
    print('%s  %d 字节' % (path, d['size']))
    print('magic=%r version=0x%04X panel=0x%04X totalPage=%d resver=0x%08X'
          % (d['magic'], d['version'], d['panel'], d['total_page'], d['resver']))
    nbad = 0
    for p in d['pages']:
        pe, ie = p['pal_entry'], p['item_entry']
        print('\n页 num=%d addr=0x%X' % (p['num'], p['addr']))
        if pe:
            print('   调色板 entry: off=0x%X 个数=%d type=0x%02X' %
                  (pe['dwOffset'], pe['wCount'], pe['bItemType']))
        for q in p['palettes']:
            print('      调色板 num=%d off=0x%X len=%d' % (q['num'], q['off'], q['len']))
        print('   条目 entry: off=0x%X 个数=%d type=0x%02X langsum=%d language=0x%X' %
              (ie['dwOffset'], ie['wCount'], ie['bItemType'], ie['langsum'], ie['language']))
        for it in p['items'][:6]:
            print('      id=%-4d fmt=%-1d(%-8s) 压缩=%d %3dx%-3d len=%-6d off=0x%-6X '
                  'hcrc %s dcrc=0x%04X'
                  % (it['id'], it['fmt'], PIXEL_FMT.get(it['fmt'], '?'), it['compress'],
                     it['w'], it['h'], it['len'], it['off'],
                     'OK' if it['crc_ok'] else 'BAD', it['data_crc']))
        nbad += sum(1 for it in p['items'] if not it['crc_ok'])
        if len(p['items']) > 6:
            print('      ... 其余 %d 条' % (len(p['items']) - 6))
    print('\nhead_crc 校验失败 %d 条' % nbad)
    used, runs = coverage(d)
    print('字节覆盖 %d/%d = %.2f%%  未覆盖 %d 段' %
          (used, d['size'], 100.0 * used / d['size'], len(runs)))
    for o, n in runs[:10]:
        print('   空洞 @0x%-6X %4d B : %s' % (o, n, d['raw'][o:o + min(n, 16)].hex()))
    return 0 if (nbad == 0 and not runs) else 1


if __name__ == '__main__':
    sys.exit(main())
