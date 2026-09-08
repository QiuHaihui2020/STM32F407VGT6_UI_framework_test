# -*- coding: utf-8 -*-
"""解析 JL.sty(= 原厂 project.bin)，验证对 .sty 容器格式的理解。

结构依据（三个来源互相印证，不是猜的）：
  1) 固件侧解析器
       User/ui_framework/lcd_drive/middle/ui_resources_manager.c : struct ui_file_head
       User/ui_framework/include/ui/control.h                    : struct ui_ctrl_info_head
  2) 原厂工具自己吐的结构转储  .../project/debug.txt
  3) ename.h 里的 ID 常量 —— 控件头里的 id 字段能逐条对上宏名

===================== 布局 =====================
0x00  ui_file_head (24 B)
      u32 ui_version      与 ename.h 的 UI_VERSION 宏一致
      u32 magic2          固定 0x6A978292
      u32 hdr_ptr         16，指向本结构后半段
      u32 total_size      文件大小减去(24 + 20*window_num)
      u8  type
      u8  window_num
      u16 prop_len        工程级属性块长度
      u8  rotate          0/1/2/3 -> 0/90/180/270
      u8  rev[3]          FF FF FF

0x18  window_head[window_num]，每项 20 B（= 固件里 sizeof(struct window_head)）
      u32 offset          该页数据块在文件里的绝对偏移
      u32 length          该页数据块总长度；offset+length == 下一页 offset
      u32 table_ptr       该页"控件索引表"的绝对偏移
      u16 table_size      索引表字节数；table_ptr+table_size == offset+length
      u16 crc1, crc2, crc3

页数据块内：
      开头是窗口自身的记录，其后是一串控件记录，靠 head.len 首尾相接。
      ui_ctrl_info_head (16 B):
        u8 type; u8 ctrl_num; u8 css_num; u8 len;
        u8 page; u8 rev[3];
        s32 id;                 // (id>>16)&0x3f = 控件类型, 低 16 位 = 名字哈希
        u32 css;                // 文件内偏移，不是内存指针
      其后 len-16 字节是类型相关负载。

用法: python sty_dump.py <JL.sty> [--json out.json]
"""
import struct
import sys
import json

# (id >> 16) & 0x3f -> 控件类型。由 ename.h 的 id 与工程 json 的 -type 对照得出。
CTRL_TYPE = {
    2: 'Window', 3: 'NewLayout', 4: 'NewLayer', 5: 'NewList/NewGrid',
    7: 'Progress', 8: 'ImageList', 9: 'Battery', 10: 'Time/Watch',
    12: 'Text', 15: 'Number',
}

HEAD_SZ = 24
WHEAD_SZ = 20
CHEAD_SZ = 16


def parse(path):
    raw = open(path, 'rb').read()
    (ver, magic2, hdr_ptr, total) = struct.unpack_from('<IIII', raw, 0)
    typ, wnum, prop_len, rot = struct.unpack_from('<BBHB', raw, 16)
    head = {
        'ui_version': ver, 'magic2': magic2, 'hdr_ptr': hdr_ptr,
        'total_size': total, 'type': typ, 'window_num': wnum,
        'prop_len': prop_len, 'rotate': rot, 'rev': raw[21:24].hex(),
        'file_size': len(raw),
    }

    wheads = []
    for i in range(wnum):
        o = HEAD_SZ + i * WHEAD_SZ
        off, ln, tptr = struct.unpack_from('<III', raw, o)
        tsz, c1, c2, c3 = struct.unpack_from('<HHHH', raw, o + 12)
        wheads.append({'index': i, 'offset': off, 'length': ln, 'table_ptr': tptr,
                       'table_size': tsz, 'crc': [c1, c2, c3]})

    def valid_head(p):
        if p + CHEAD_SZ > len(raw):
            return False
        t, cnum, cssn, ln = raw[p], raw[p + 1], raw[p + 2], raw[p + 3]
        if ln < CHEAD_SZ or ln > 208 or p + ln > len(raw):
            return False
        if t == 0 or t > 63:
            return False
        if raw[p + 5:p + 8] != b'\xff\xff\xff':
            return False
        cid = struct.unpack_from('<i', raw, p + 8)[0]
        return cid > 0 and ((cid >> 16) & 0x3F) == t

    for w in wheads:
        # 窗口记录长度不固定，向后扫到第一个能自洽的控件头为止
        start = None
        for p in range(w['offset'], min(w['offset'] + 64, len(raw)), 4):
            if valid_head(p):
                start = p
                break
        w['window_record'] = raw[w['offset']:start].hex() if start else ''
        ctrls = []
        p = start if start else w['offset']
        end = w['table_ptr']
        while p < end and valid_head(p):
            ln = raw[p + 3]
            cid = struct.unpack_from('<i', raw, p + 8)[0]
            ctrls.append({
                'off': p, 'type': raw[p], 'type_name': CTRL_TYPE.get(raw[p], '?'),
                'ctrl_num': raw[p + 1], 'css_num': raw[p + 2], 'len': ln,
                'page': raw[p + 4], 'id': cid & 0xFFFFFF,
                'id_type': (cid >> 16) & 0x3F, 'id_hash': cid & 0xFFFF,
                'css_off': struct.unpack_from('<I', raw, p + 12)[0],
                'payload': raw[p + CHEAD_SZ:p + ln].hex(),
            })
            p += ln
        w['scan_stopped_at'] = p
        w['controls'] = ctrls
    return {'head': head, 'windows': wheads}


def main():
    doc = parse(sys.argv[1])
    h = doc['head']
    print('== ui_file_head ==')
    print('  UI_VERSION = 0x%08X    magic2 = 0x%08X' % (h['ui_version'], h['magic2']))
    print('  hdr_ptr=%d  total_size=%d  type=%d  window_num=%d  prop_len=%d  rotate=%d'
          % (h['hdr_ptr'], h['total_size'], h['type'], h['window_num'],
             h['prop_len'], h['rotate']))
    print('  文件 %d B；24 + 20*%d + total_size = %d  -> %s'
          % (h['file_size'], h['window_num'],
             HEAD_SZ + WHEAD_SZ * h['window_num'] + h['total_size'],
             '一致' if HEAD_SZ + WHEAD_SZ * h['window_num'] + h['total_size'] == h['file_size']
             else '不一致'))

    print('\n== window_head 表（与 debug.txt 的 offset/prop_len/t_ptr/t_size/crc 逐列对拍）==')
    print('  %-3s %-8s %-8s %-8s %-6s %s' % ('#', 'offset', 'length', 't_ptr', 't_size', 'crc'))
    for w in doc['windows']:
        print('  %-3d %-8X %-8X %-8X %-6X %s'
              % (w['index'], w['offset'], w['length'], w['table_ptr'], w['table_size'],
                 ' '.join('%X' % c for c in w['crc'])))
    print('  链式校验:')
    for i, w in enumerate(doc['windows']):
        nxt = doc['windows'][i + 1]['offset'] if i + 1 < len(doc['windows']) else h['file_size']
        a = w['offset'] + w['length']
        b = w['table_ptr'] + w['table_size']
        print('    页%d: offset+length=0x%X, t_ptr+t_size=0x%X, 下一页/文件尾=0x%X  -> %s'
              % (i, a, b, nxt, '一致' if a == b == nxt else '不一致'))

    for w in doc['windows']:
        print('\n== window[%d] @0x%X  窗口记录 %d B  控件 %d 条  扫描止于 0x%X (表在 0x%X)'
              % (w['index'], w['offset'], len(w['window_record']) // 2,
                 len(w['controls']), w['scan_stopped_at'], w['table_ptr']))
        for c in w['controls'][:8]:
            print('   0x%-6X t=%-2d %-14s len=%-3d id=0x%06X (hash=0x%04X) css=0x%X'
                  % (c['off'], c['type'], c['type_name'], c['len'], c['id'],
                     c['id_hash'], c['css_off']))
        if len(w['controls']) > 8:
            print('   ... 其余 %d 条略' % (len(w['controls']) - 8))

    if len(sys.argv) > 3 and sys.argv[2] == '--json':
        json.dump(doc, open(sys.argv[3], 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
        print('\n完整结构 -> %s' % sys.argv[3])


if __name__ == '__main__':
    main()
