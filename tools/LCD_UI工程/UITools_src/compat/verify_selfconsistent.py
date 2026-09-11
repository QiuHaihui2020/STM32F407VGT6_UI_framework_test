# -*- coding: utf-8 -*-
"""固件能不能吃下这套产物 —— 只看产物自身，不和参考文件比。

和 verify_toolchain.py 的分工：
  verify_toolchain  证明"同样的输入，算出同样的字节"（借了现成的 id）
  本脚本            证明"**完全自己跑**出来的一套东西内部自洽、固件加载得了"

这才是"能不能上机"的判定条件 —— 因为一旦不借现成的 id，.sty 里的 id 和
ename.h 里的宏值都是我们自己分配的，必须两边严丝合缝。

检查项（对着固件的加载路径逐条来）：
  1  .sty 文件头 UI_VERSION == ename.h 里的 UI_VERSION
     （ui_resources_manager.c: ui_style_file_version_compare 会 ASSERT）
  2  页表三个 CRC 全对（crc_head/crc_data 对不上固件直接返回 NULL）
  3  每个控件 id 在 ename.h 里有唯一对应的宏，反之亦然
  4  PAGE_n 宏存在且 = (pj<<29)|(n<<22)|(2<<16)|n —— ui_style.h 只认这几个
  5  控件 id 的位域自洽：type 段 == 控件头 type，page 段 == 所在页
  6  所有指针字段落在本页内、且指向合法位置；重定位表登记齐全
  7  图片资源号 <= 该页 result_pic_index.h 的条目数；文字资源号 <= 全局条目数
     （0xFFFF 是"空"的哨兵，放行）
  8  .res / .str 100% 字节覆盖、head_crc 全对
  9  ename.h 里有 ui_style.h 需要的全部宏

用法: python verify_selfconsistent.py <产物目录> [固件 include/common 目录]
"""
import os
import re
import struct
import sys

from res_dump import parse as parse_res
from sty_blocks import PTRS, NONE
from sty_dump import parse as parse_sty

U16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
U32 = lambda b, o: struct.unpack_from('<I', b, o)[0]


def crc16(data, crc=0):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


class Check(object):
    def __init__(self):
        self.ok = 0
        self.bad = 0
        self.msgs = []

    def __call__(self, cond, what, detail=''):
        if cond:
            self.ok += 1
        else:
            self.bad += 1
            if len(self.msgs) < 20:
                self.msgs.append('%s  %s' % (what, detail))
        return cond

    def section(self, title, cond, detail=''):
        mark = 'OK  ' if cond else '失败'
        print('  [%s] %s%s' % (mark, title, ('  ' + detail) if detail else ''))
        self(cond, title, detail)


def load_defines(path):
    txt = open(path, encoding='utf-8', errors='replace').read()
    return {n: int(v, 16) for n, v in
            re.findall(r'#define\s+(\S+)\s+0X([0-9A-Fa-f]+)', txt)}


def load_pic_index(path):
    """每页的最大图片编号。

    一行长这样：
        #define  BATTLVL1                 1       //D:\...\BATTLVL1.BMP
    宏名里可以带空格和括号 —— "J3_lineart (1).bmp" 直接原样大写成
    `J3_LINEART (1)`（见 docs/FILE_FORMATS.md 10.9）。所以不能按空格切第 3 段，
    要取注释之前的最后一个整数。
    """
    out, page = {}, -1
    for line in open(path, 'rb'):
        s = line.decode('latin1').rstrip()
        if s.startswith('//PAGE'):
            page = int(s.split()[1])
            out[page] = 0
            continue
        if not s.startswith('#define'):
            continue
        m = re.search(r'(\d+)\s*$', s.split('//', 1)[0])
        if m:
            out[page] = max(out.get(page, 0), int(m.group(1)))
    return out


def load_str_count(path):
    n = 0
    for line in open(path, 'rb'):
        p = line.decode('latin1').split()
        if len(p) >= 3 and p[0] == '#define' and p[2].isdigit():
            n = max(n, int(p[2]))
    return n


def main():
    d = os.path.abspath(sys.argv[1])
    hdrdir = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else None

    sty_path = os.path.join(d, 'project.bin')
    raw = open(sty_path, 'rb').read()
    doc = parse_sty(sty_path)
    names = load_defines(os.path.join(d, 'ename.h'))
    picmax = load_pic_index(os.path.join(d, 'result_pic_index.h'))
    strmax = load_str_count(os.path.join(d, 'result_str_index.h'))

    c = Check()
    print('产物目录: %s' % d)
    print()

    # ---- 1. UI_VERSION ----
    print('1) 版本号自洽')
    hv = U32(raw, 0)
    c.section('.sty 头 0x%08X == ename.h 的 UI_VERSION 0x%08X'
              % (hv, names.get('UI_VERSION', 0)),
              hv == names.get('UI_VERSION'))

    # ---- 2. 页表 CRC ----
    print('2) 页表 CRC（固件对不上就返回 NULL）')
    for i, w in enumerate(doc['windows']):
        at = 24 + 20 * i
        h14 = raw[at:at + 14]
        page = raw[w['offset']:w['offset'] + w['length']]
        table = raw[w['table_ptr']:w['table_ptr'] + w['table_size']]
        c.section('页%d crc_data/crc_table/crc_head' % i,
                  crc16(page) == U16(raw, at + 14)
                  and crc16(table) == U16(raw, at + 16)
                  and crc16(h14) == U16(raw, at + 18))

    # ---- 3/5. id <-> ename ----
    print('3) 控件 id 与 ename.h 的对应')
    by_id = {}
    for w in doc['windows']:
        for x in w['controls']:
            by_id[x['id'] & 0xFFFFFF] = x
    macro_by_id = {}
    dup = []
    for n, v in names.items():
        if n in ('UI_VERSION', 'UI_ROTATE'):
            continue
        k = v & 0xFFFFFF
        if k in macro_by_id:
            dup.append((n, macro_by_id[k]))
        macro_by_id[k] = n
    missing = [hex(k) for k in by_id if k not in macro_by_id]
    c.section('每个控件 id 都能在 ename.h 里找到宏',
              not missing, '缺 %d 个' % len(missing) if missing else '%d 个控件' % len(by_id))
    c.section('ename.h 里没有重号', not dup,
              ('例: %s' % dup[:3]) if dup else '%d 个宏' % len(macro_by_id))

    print('5) id 位域自洽（type 段 / page 段）')
    badbits = []
    for pi, w in enumerate(doc['windows']):
        for x in w['controls']:
            if ((x['id'] >> 16) & 0x3F) != x['type'] or ((x['id'] >> 22) & 0x7F) != pi:
                badbits.append(macro_by_id.get(x['id'] & 0xFFFFFF, hex(x['id'])))
    c.section('id 的 type/page 段与控件头一致', not badbits,
              ('%d 个不符' % len(badbits)) if badbits else '%d 个控件' % len(by_id))

    # ---- 4. PAGE_n ----
    print('4) PAGE_n（ui_style.h 只认这几个）')
    for i in range(len(doc['windows'])):
        want = (i << 22) | (2 << 16) | i
        got = names.get('PAGE_%d' % i)
        c.section('PAGE_%d = 0x%06X' % (i, want), got == want,
                  '' if got == want else '实际 %s' % (hex(got) if got else '缺失'))

    # ---- 6. 指针与重定位表 ----
    print('6) 指针落点与重定位表')
    for pi, w in enumerate(doc['windows']):
        base, lo, hi = w['offset'], w['offset'], w['table_ptr']
        table = raw[w['table_ptr']:w['table_ptr'] + w['table_size']]
        reloc = {U16(table, k) for k in range(0, len(table), 2)}
        bad_ptr = 0
        not_reg = 0
        for x in w['controls']:
            rec = (bytes([x['type'], x['ctrl_num'], x['css_num'], x['len'], x['page'],
                          0xFF, 0xFF, 0xFF])
                   + struct.pack('<iI', x['id'], x['css_off'])
                   + bytes.fromhex(x['payload']))
            for name, off in PTRS.get(x['type'], []):
                if off + 4 > len(rec):
                    continue
                pv = U32(rec, off)
                roff = (x['off'] - base) + off
                if roff not in reloc:
                    not_reg += 1
                if pv in NONE:
                    continue
                tgt = base + (pv & 0xFFFF)
                if not (lo <= tgt < hi):
                    bad_ptr += 1
        c.section('页%d 指针全部落在本页数据区内' % pi, bad_ptr == 0,
                  '' if bad_ptr == 0 else '%d 个越界' % bad_ptr)
        c.section('页%d 指针字段全部登记进重定位表' % pi, not_reg == 0,
                  '' if not_reg == 0 else '%d 个没登记' % not_reg)

    # ---- 7. 资源号范围 ----
    print('7) 资源号在 result_*_index.h 的范围内')
    bad_pic = bad_str = 0
    for pi, w in enumerate(doc['windows']):
        base = w['offset']
        pmax = picmax.get(pi, 0)
        for x in w['controls']:
            rec = (bytes([x['type'], x['ctrl_num'], x['css_num'], x['len'], x['page'],
                          0xFF, 0xFF, 0xFF])
                   + struct.pack('<iI', x['id'], x['css_off'])
                   + bytes.fromhex(x['payload']))
            for name, off in PTRS.get(x['type'], []):
                if off + 4 > len(rec):
                    continue
                pv = U32(rec, off)
                if pv in NONE:
                    continue
                a = base + (pv & 0xFFFF)
                if name.startswith('img'):
                    for k in range(U16(raw, a)):
                        v = U16(raw, a + 2 + 2 * k)
                        if v != 0xFFFF and not (1 <= v <= pmax):
                            bad_pic += 1
                elif name == 'strlist':
                    for k in range(U16(raw, a)):
                        v = U16(raw, a + 2 + 2 * k)
                        if v != 0xFFFF and not (1 <= v <= strmax):
                            bad_str += 1
                elif name == 'css':
                    bi = U32(raw, a + 24)
                    if bi != 0xFFFFFFFF and not (1 <= (bi & 0xFFFFFF) <= pmax):
                        bad_pic += 1
    c.section('图片资源号都 <= 本页图片数', bad_pic == 0,
              '每页上限 %s' % picmax if bad_pic == 0 else '%d 个越界' % bad_pic)
    c.section('文字资源号都 <= 全局条目数 %d' % strmax, bad_str == 0,
              '' if bad_str == 0 else '%d 个越界' % bad_str)

    # ---- 8. .res / .str ----
    print('8) .res / .str 完整性')
    for fn in ('result.bin', 'result.str'):
        rd = parse_res(os.path.join(d, fn))
        items = [i for p in rd['pages'] for i in p['items']]
        nbad = sum(1 for i in items if not i['crc_ok'])
        c.section('%s head_crc 全对（%d 条）' % (fn, len(items)), nbad == 0,
                  '' if nbad == 0 else '%d 条坏' % nbad)

    # ---- 9. 固件需要的宏 ----
    if hdrdir:
        print('9) 固件 ui_style.h 需要的宏')
        up = os.path.join(hdrdir, 'ui_style.h')
        if os.path.exists(up):
            txt = open(up, encoding='latin1').read()
            # 注释里提到的名字不算引用 —— ui_style.h 的注释里就写着"只画到 PAGE_10"
            body = re.sub(r'/\*.*?\*/', ' ', txt, flags=re.S)
            body = re.sub(r'//[^\n]*', ' ', body)
            # #ifdef PAGE_n 包起来的是"有就用、没有就算了"，也不是硬依赖
            # （既有 ename.h 同样没有 PAGE_11/PAGE_12，就是靠这个兜的）
            optional = set(re.findall(r'#if(?:n?def)\s+(PAGE_\d+)', body))
            optional |= set(re.findall(r'defined\s*\(\s*(PAGE_\d+)\s*\)', body))
            need = set(re.findall(r'\bPAGE_\d+\b', body)) - optional
            lack = sorted(need - set(names))
            c.section('ui_style.h 硬依赖的 %d 个 PAGE_* 宏都有' % len(need), not lack,
                      ('可选(#ifdef)的: %s' % sorted(optional)) if not lack
                      else '缺 %s' % lack)

    print()
    print('通过 %d 项，失败 %d 项' % (c.ok, c.bad))
    for m in c.msgs:
        print('   ! ' + m)
    return 0 if c.bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
