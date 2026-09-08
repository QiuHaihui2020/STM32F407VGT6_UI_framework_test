# -*- coding: utf-8 -*-
"""扫描 Qt5 moc 产物: qt_meta_stringdata_* 字面量块 + staticMetaObject。

QByteArrayData (32bit) = { int ref; int size; uint alloc:31,cr:1; qptrdiff offset; } = 16B
moc 里 offset 是相对于该 QByteArrayData 元素自身地址的。
"""
import struct
import sys
import json
from pe import Img

QBAD = 16


def scan_stringdata(img):
    """返回 {块起始VA: [字符串,...]}"""
    blocks = {}
    for secname in ('.rdata', '.data'):
        s = img.sec(secname)
        if not s:
            continue
        _, vstart, vsize, poff, psize = s
        raw = img.raw
        i = poff
        end = poff + psize - QBAD
        cur = None
        cur_va = None
        while i <= end:
            ref, size, alloc, off = struct.unpack_from('<iIIi', raw, i)
            ok = False
            body = b''
            # size==0 是 moc 常发的空串字面量，必须接受，否则会把一个块切成碎片
            if ref == -1 and alloc == 0 and 0 <= size < 4096:
                va = vstart + (i - poff)
                tva = va + off
                to = img.va2off(tva)
                if to is not None and to + size < len(raw) and raw[to + size:to + size + 1] == b'\x00':
                    body = raw[to:to + size]
                    if b'\x00' not in body:
                        ok = True
            if ok:
                txt = body.decode('utf-8', 'replace')
                if cur is None:
                    cur = []
                    cur_va = va
                cur.append(txt)
                i += QBAD
            else:
                if cur:
                    blocks[cur_va] = cur
                    cur = None
                i += 4
        if cur:
            blocks[cur_va] = cur
    return blocks


# ---- QMetaObject data 解码 (revision 7 / Qt 5.9) ----
# Qt5 QMetaType::Type —— 注意 Void=43 / VoidStar=31，不是 0
MT = {0: 'UnknownType', 1: 'bool', 2: 'int', 3: 'uint', 4: 'qlonglong', 5: 'qulonglong',
      6: 'double', 7: 'QChar', 8: 'QVariantMap', 9: 'QVariantList', 10: 'QString',
      11: 'QStringList', 12: 'QByteArray', 13: 'QBitArray', 14: 'QDate', 15: 'QTime',
      16: 'QDateTime', 17: 'QUrl', 18: 'QLocale', 19: 'QRect', 20: 'QRectF',
      21: 'QSize', 22: 'QSizeF', 23: 'QLine', 24: 'QLineF', 25: 'QPoint',
      26: 'QPointF', 27: 'QRegExp', 28: 'QVariantHash', 29: 'QEasingCurve',
      30: 'QUuid', 31: 'void*', 32: 'long', 33: 'short', 34: 'char', 35: 'ulong',
      36: 'ushort', 37: 'uchar', 38: 'float', 39: 'QObject*', 40: 'signed char',
      41: 'QVariant', 42: 'QModelIndex', 43: 'void', 44: 'QRegularExpression',
      45: 'QJsonValue', 46: 'QJsonObject', 47: 'QJsonArray', 48: 'QJsonDocument',
      49: 'QByteArrayList', 50: 'QPersistentModelIndex', 51: 'std::nullptr_t',
      64: 'QFont', 65: 'QPixmap', 66: 'QBrush', 67: 'QColor', 68: 'QPalette',
      69: 'QIcon', 70: 'QImage', 71: 'QPolygon', 72: 'QRegion', 73: 'QBitmap',
      74: 'QCursor', 75: 'QKeySequence', 76: 'QPen', 77: 'QTextLength',
      78: 'QTextFormat', 79: 'QMatrix', 80: 'QTransform', 81: 'QMatrix4x4',
      82: 'QVector2D', 83: 'QVector3D', 84: 'QVector4D', 85: 'QQuaternion',
      86: 'QPolygonF', 87: 'QColorSpace', 121: 'QSizePolicy'}

MF_ACCESS = {0: 'private', 1: 'protected', 2: 'public'}
MethodMethod, MethodSignal, MethodSlot, MethodConstructor = 0x00, 0x04, 0x08, 0x0c


def typename(t, strs):
    if t & 0x80000000:
        return strs[t & 0x7fffffff]
    return MT.get(t, 'type%d' % t)


def decode_metaobject(img, data_va, strs):
    def D(i):
        return img.u32(data_va + 4 * i)

    rev = D(0)
    out = {'revision': rev, 'classname': strs[D(1)] if D(1) < len(strs) else '?'}
    ncls, ocls = D(2), D(3)
    nm, om = D(4), D(5)
    npr, opr = D(6), D(7)
    nen, oen = D(8), D(9)
    nct, oct_ = D(10), D(11)
    out['flags'] = D(12)
    nsig = D(13)

    out['classinfo'] = []
    for i in range(ncls):
        k, v = D(ocls + 2 * i), D(ocls + 2 * i + 1)
        out['classinfo'].append([strs[k], strs[v]])

    def methods(count, off, kindname):
        res = []
        for i in range(count):
            b = off + 5 * i
            name, argc, pofs, tag, flags = D(b), D(b + 1), D(b + 2), D(b + 3), D(b + 4)
            ret = typename(D(pofs), strs)
            args = []
            for a in range(argc):
                at = typename(D(pofs + 1 + a), strs)
                an = strs[D(pofs + 1 + argc + a)] if D(pofs + 1 + argc + a) < len(strs) else 'a%d' % a
                args.append([at, an])
            mtype = flags & 0x0c
            kind = {0x00: 'method', 0x04: 'signal', 0x08: 'slot', 0x0c: 'constructor'}[mtype]
            if kindname == 'ctor':
                kind = 'constructor'
            res.append({'name': strs[name], 'return': ret, 'args': args,
                        'access': MF_ACCESS.get(flags & 3, '?'), 'kind': kind,
                        'tag': strs[tag] if tag < len(strs) else '', 'flags': flags})
        return res

    out['methods'] = methods(nm, om, 'm')
    out['signalCount'] = nsig
    out['constructors'] = methods(nct, oct_, 'ctor')

    out['properties'] = []
    for i in range(npr):
        b = opr + 3 * i
        out['properties'].append({'name': strs[D(b)], 'type': typename(D(b + 1), strs),
                                  'flags': D(b + 2)})

    out['enums'] = []
    for i in range(nen):
        b = oen + 4 * i
        ename, eflags, ecount, edata = D(b), D(b + 1), D(b + 2), D(b + 3)
        vals = []
        for k in range(ecount):
            vals.append([strs[D(edata + 2 * k)], D(edata + 2 * k + 1)])
        out['enums'].append({'name': strs[ename], 'isFlag': bool(eflags & 1), 'values': vals})
    return out


def scan_metaobjects(img, blocks):
    """staticMetaObject = 6 个指针: superdata, stringdata, data, static_metacall, related, extradata"""
    found = []
    bset = set(blocks.keys())
    for secname in ('.rdata', '.data'):
        s = img.sec(secname)
        if not s:
            continue
        _, vstart, vsize, poff, psize = s
        raw = img.raw
        for i in range(poff, poff + psize - 24, 4):
            sup, sd, dt = struct.unpack_from('<III', raw, i)
            if sd not in bset:
                continue
            if img.va2off(dt) is None:
                continue
            rev = img.u32(dt)
            if rev not in (7, 8):
                continue
            va = vstart + (i - poff)
            found.append({'va': va, 'super_va': sup, 'stringdata_va': sd, 'data_va': dt})
    return found


def main():
    path = sys.argv[1]
    out = sys.argv[2]
    img = Img(path)
    blocks = scan_stringdata(img)
    print('stringdata blocks:', len(blocks))
    mos = scan_metaobjects(img, blocks)
    print('staticMetaObject candidates:', len(mos))

    by_va = {}
    classes = []
    fails = {}
    for m in mos:
        strs = blocks[m['stringdata_va']]
        try:
            d = decode_metaobject(img, m['data_va'], strs)
        except Exception as e:
            fails.setdefault(type(e).__name__ + ': ' + str(e)[:60], []).append(strs[0])
            continue
        d['va'] = m['va']
        d['super_va'] = m['super_va']
        classes.append(d)
        by_va[m['va']] = d

    def name_of_mo(va):
        """任意 staticMetaObject VA -> 类名（不要求它已被解码）"""
        if not va:
            return None
        d = by_va.get(va)
        if d:
            return d['classname']
        sd = img.u32(va + 4)
        if sd is None:
            return '0x%08x' % va
        if sd in blocks:
            return blocks[sd][0]
        # sd 可能指向块内部（不是块首）
        for bva, bstrs in blocks.items():
            if bva <= sd < bva + 16 * len(bstrs):
                return bstrs[(sd - bva) // 16]
        return '0x%08x' % va

    for c in classes:
        c['super'] = name_of_mo(c['super_va'])
    print('decoded classes:', len(classes))
    if fails:
        print('decode failures:')
        for k, v in sorted(fails.items(), key=lambda kv: -len(kv[1]))[:10]:
            print('   %-70s x%d  e.g. %s' % (k, len(v), v[:3]))
    with open(out, 'w', encoding='utf-8') as f:
        json.dump({'classes': classes,
                   'blocks': {('0x%08x' % k): v for k, v in blocks.items()}},
                  f, ensure_ascii=False, indent=1)


if __name__ == '__main__':
    main()
