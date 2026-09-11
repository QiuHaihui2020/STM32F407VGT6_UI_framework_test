# -*- coding: utf-8 -*-
"""整链验收：用重建版 QtToolBin + ResBuilder 跑一遍，和原厂产物逐字节对比。

    python verify_toolchain.py <重建版 bin 目录> <工程目录> [输出目录]

例：
    python verify_toolchain.py C:\\bt\\uitools ..\\..\\ui_128_64_JL02\\模式界面\\project C:\\bt\\chain

会打印一张表，列出每个产物"差多少字节、差在哪"。已知且**不可能消除**的差异
（2026-09-09 把原厂 QtToolBin 在同一份工程上连跑三遍逐项比对确认，
详见 docs/FILE_FORMATS.md 10.10 / 10.11）：

  project.bin  Time(+28) / number(+24) 的 char format[16]，原厂只 strcpy 了
               strlen+1 个字节，剩下十来个字节是没清的堆内存（里面躺着两个
               活指针）。原厂自己重跑一遍，那些指针也全变。本版整片清零。
               外加没有子控件的 NewLayout：原厂在子指针里留了兄弟指针的值，
               本版写 0（固件 layer.c 先判 ctrl_num 再解引用，写 0 是安全的）。
  result.bin   4 字节 resver（原厂随机值）
               + 调色板的**排列顺序**：原厂 <ColorList> 的次序来自 Qt5 QSet
                 迭代，随机哈希种子，跑三遍三个样。集合一致，OSD1 单色屏也
                 根本不查调色板。本版用确定性排序，同工程跑多少遍都一样。
  result.str   4 字节 resver
  result.xml   同样只差 <Color> 的排序
  result_str_index.h  时间戳 + 少数几行的**位置**（原厂 <CellList> 也是随机序）
  res_ver.h / result_pic_index.h   生成时间戳

除此之外应当**逐字节相同** —— 图片的编号和像素、字符串的宽高和点阵、
ename.h / result.h / result.csv 都必须一个字节不差。

【要命的一点：参考产物就躺在工程目录里，会被覆盖】
这里说的"原厂"，指的是工程目录里现成的那几个文件（project.bin / result.* …）。
它们是原厂工具当初生成后留在那儿的，**不受 git 管**（.gitignore 里有 *.bin），
而任何一次 step2（不管用原厂工具还是本版）都会就地把它们重写一遍。

也就是说：拿另一个工程跑一次 step2，参考就被换成那个工程的产物了，
再跑本脚本会报一堆假的"超出预期"。2026-09-08 16:19 就发生过一次 ——
project.bin 被换成 oled 工程的（45248 字节），而脚本在建 TFT（24498 字节）。

判断方法：失败时脚本会把各参考文件的生成时间列出来，时间明显不一致的那几个
就是被覆盖的。要恢复真正的原厂参考，只能用 **原厂** UITools/QtToolBin.exe
对着同一个工程再跑一次 step2（本版工具生成的不能当参考用 —— 拿自己比自己，
比过了也说明不了什么）。
"""
import datetime
import io
import os
import re
import subprocess
import sys

FILES = [
    'project.bin', 'ename.h', 'result.bin', 'result.str', 'result.h',
    'res_ver.h', 'result_pic_index.h', 'result_str_index.h',
    'result.csv', 'result.xml',
]

# 每个文件允许的差异上限（字节），超了就算失败。
#
# 这几个数是按"最坏情况能差多少"估的，不是实测值 —— 实测应当远小于它：
#   project.bin  每个 Time / number 控件最多漏 12 字节未初始化尾巴，
#                oled 工程 15 + 12 个这种控件 = 324；再加页表 CRC 和时间戳。
#   result.bin   resver 4 字节 + 每页调色板 1024 字节全排错（11 页）。
#   result.str   只有 resver。位图但凡差一个字节就是真错，不给预算。
#   result.xml   <Color> 排序，每页十来行 × 每行几十字节。
#   result_str_index.h  <CellList> 随机序挪动的行。
# 原来这几个数是照 4 页的 TFT 工程定的，11 页的 oled 工程直接撑爆，
# 那是预算的问题不是产物的问题（差异逐条归类过，见 FILE_FORMATS.md 10.11）。
BUDGET = {
    'project.bin': 512,
    'result.bin': 4 + 1024 * 12,
    'result.str': 4,
    'res_ver.h': 32,
    'result_pic_index.h': 8,
    'result_str_index.h': 256,
    'result.xml': 8192,
}


HERE_REFS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'refs')

# 文本产物里"允许整行不同"的判据。命中就算已知差异，其余一律不可解释。
_TS = re.compile(r'\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}')


def _segs(a, b):
    """差异按连续段归并，返回 [(起, 止)]（闭区间，8 字节内算一段）。"""
    d = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
    out = []
    if not d:
        return out
    s0 = p = d[0]
    for i in d[1:]:
        if i <= p + 8:
            p = i
        else:
            out.append((s0, p))
            s0 = p = i
    out.append((s0, p))
    return out


def explain_text(name, a, b):
    """文本产物按**行**比。返回不可解释的行（行号, 本版, 原厂）。

    只允许两类整行不同：
      · 行里有时间戳（生成时间，每次都变）
      · result.xml 的 <Color .../> —— 原厂 <ColorList> 的次序来自 Qt5 QSet
        的随机哈希种子，跑三遍三个样（FILE_FORMATS.md 10.10）
    """
    la = a.decode('utf-8', 'replace').splitlines()
    lb = b.decode('utf-8', 'replace').splitlines()
    bad = []
    if len(la) != len(lb):
        bad.append((-1, '行数 %d' % len(la), '行数 %d' % len(lb)))
        return bad
    for i, (x, y) in enumerate(zip(la, lb)):
        if x == y:
            continue
        if _TS.search(x) and _TS.search(y):
            continue                              # 生成时间
        if name == 'result.xml' and '<Color' in x and '<Color' in y:
            continue                              # 调色板次序
        if name == 'res_ver.h' and '#define' in x and '#define' in y:
            continue                              # 版本号/校验和，跟 resver 走
        bad.append((i + 1, x.strip()[:70], y.strip()[:70]))
    return bad


def explain_sty(a, b):
    """project.bin 只允许三种差异，别的都算不可解释：

      · [4,8)            生成时间戳（固件把前 16 字节当 res[16] 不透明块）
      · 每页表项 +14..15 crc_data —— 页数据里有下面那种垃圾尾巴，CRC 自然跟着变
      · 本版写 0x00、原厂是垃圾的连续段 —— Time(+28)/number(+24) 的
        char format[16]，原厂只 strcpy 了 strlen+1 个字节，剩下是没清的堆内存
        （里面躺着两个活指针，原厂自己重跑一遍也全变）；本版整片清零。
    """
    npg = a[17] if len(a) > 17 else 0
    crc_at = set()
    for i in range(npg):
        base = 24 + i * 20 + 14
        crc_at.update((base, base + 1))
    bad = []
    for s0, p in _segs(a, b):
        if s0 >= 4 and p < 8:
            continue
        if all(i in crc_at for i in range(s0, p + 1)):
            continue
        if all(a[i] == 0 for i in range(s0, p + 1)):
            continue                              # 本版清零 vs 原厂垃圾
        bad.append((s0, p, a[s0:p + 1][:12].hex(' '), b[s0:p + 1][:12].hex(' ')))
    return bad


def explain_res(a, b):
    """result.bin / result.str 允许两种差异，别的都算不可解释：

      · [0x0C,0x10)  resver —— 原厂是随机值，跑三遍三个样
      · **调色板次序** —— 原厂 <ColorList> 的次序来自 Qt5 QSet 的随机哈希
        种子（FILE_FORMATS.md 10.10）。这里不是无脑放行：把差异段按 4 字节
        （一个颜色项）切开，两边排序后必须**完全一样**，也就是说只准是同一
        组颜色换了个顺序。少一个、多一个、改一个值，立刻判不可解释。
    """
    bad = []
    for s0, p in _segs(a, b):
        if s0 >= 0x0C and p < 0x10:
            continue
        # 【四种相位都试】调色板在文件里的起点不按文件的 4 字节网格走
        # （每张图的块头长度不一样），所以不能拿 s0 % 4 去对齐 —— 实测
        # 0x1C3B 这一段就是错位的。四个相位里只要有一个能证明"同一组颜色
        # 换了次序"，就算解释得通。
        okperm = False
        for phase in range(4):
            lo = s0 - ((s0 - phase) % 4)
            hi = p + 1
            hi += (-(hi - phase)) % 4
            if lo < 0 or hi > min(len(a), len(b)):
                continue
            ca = [a[i:i + 4] for i in range(lo, hi, 4)]
            cb = [b[i:i + 4] for i in range(lo, hi, 4)]
            if ca and sorted(ca) == sorted(cb):
                okperm = True
                break
        if okperm:
            continue                      # 同一组颜色，只是次序不同
        bad.append((s0, p, a[s0:p + 1][:12].hex(' '), b[s0:p + 1][:12].hex(' ')))
    return bad


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    bindir, proj = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    outdir = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else os.path.join(bindir, 'chain')
    os.makedirs(outdir, exist_ok=True)

    jsons = [f for f in os.listdir(proj)
             if f.lower().endswith('.json') and 'autosave' not in f.lower()
             and f.lower() != 'version.txt']
    if not jsons:
        print('工程目录里没有 json')
        return 2

    # 第 4 个参数可以点名要跑哪个 json
    pick = sys.argv[4] if len(sys.argv) > 4 else None
    if pick is None:
        # 【按 project.ini 挑，不要按文件名排序取第一个】
        # 工程目录里同时躺着 SmallColorTFT.json 和 SmallColor_oled.json，
        # 而目录里那些参考产物（project.bin / result.* …）是**上一次 step2
        # 生成的那个工程**留下的 —— 谁最后生成就是谁的。
        # 原来取 jsons[0]（字典序＝TFT），一旦有人用 oled 跑过一次 step2，
        # 就变成"拿 TFT 的产物去比 oled 的参考"，报一堆假的不合格。
        # QtToolBin 自己不带 json 参数时读的就是 project.ini，这里跟它一致。
        ini = os.path.join(proj, 'config', 'ini', 'project.ini')
        if os.path.exists(ini):
            for line in io.open(ini, encoding='utf-8', errors='replace'):
                if line.strip().lower().startswith('projectfilename'):
                    pick = line.split('=', 1)[1].strip()
                    break
    if not pick or not os.path.exists(os.path.join(proj, pick)):
        pick = jsons[0]
    jsonp = os.path.join(proj, pick)
    print('工程: %s' % pick)
    xls = os.path.abspath(os.path.join(proj, '..', '..', '..', 'UITools',
                                       '多国语言_128_64.xls'))

    # 【被顶掉的 .bin 参考先从存档还原】
    # 别的产物是 tracked 的，被顶掉 `git checkout` 就能回来；而 *.bin 在
    # .gitignore 里，一旦被别的运行覆盖就**永远回不来**，而且不会有任何提示。
    # 实际吃过两次亏：
    #   project.bin  一天之内被顶两次（24692 -> 26108 -> 26400）
    #   result.bin   被 rebuilt 工程的产物顶成 11451（真值 10953），而旧的
    #                "差异字节数 <= 预算"判定还一路报"允许范围内" —— 假绿
    # 所以在 re/refs/ 存了**原厂工具当场生成**的那一份，跑之前对一下就还原。
    # 存档带 .ref 后缀：仓库的 .gitignore 里有 *.bin，不改名就提交不上去。
    stem = os.path.basename(pick).replace('.json', '')
    for prod in ('project.bin', 'result.bin'):
        archive = os.path.join(HERE_REFS, '%s.%s.ref' % (stem, prod))
        live = os.path.join(proj, prod)
        if not os.path.exists(archive):
            continue
        want = open(archive, 'rb').read()
        cur = open(live, 'rb').read() if os.path.exists(live) else None
        if cur != want:
            open(live, 'wb').write(want)
            print('（%s 参考被顶过，已从 re/refs 存档还原：%d -> %d 字节）'
                  % (prod, len(cur) if cur else 0, len(want)))

    # 【跑之前把参考产物拍个快照，跑完原样放回去】
    # ResBuilder 的产物是"就地"落在工程目录里的（-o 只管得住 QtToolBin），
    # 于是每跑一次校验，工程目录里那份**原厂参考就被自己的输出顶掉**。
    # 更糟的是拿 oled 跑一次，TFT 的参考就成了 oled 的产物，下次比对
    # 报一堆假的不合格 —— 2026-09-10 就这么把 ename.h 从 8644 顶成 9101。
    # 而 ui_128_64_JL02 是**原厂目录，一个字节都不该动**。
    # 快照存在内存里，比对也用快照，跑完再写回去（内容没变就不写）。
    snapshot = {}
    for n in FILES + ['Resbuilder.xml', 'debug.txt', 'imagelist.txt']:
        p = os.path.join(proj, n)
        if os.path.exists(p):
            snapshot[n] = open(p, 'rb').read()

    def restore():
        restored = []
        for n, data in snapshot.items():
            p = os.path.join(proj, n)
            try:
                cur = open(p, 'rb').read() if os.path.exists(p) else None
            except OSError:
                cur = None
            if cur != data:
                open(p, 'wb').write(data)
                restored.append(n)
        if restored:
            print('（已把工程目录里被覆盖的参考恢复原样：%s）' % ', '.join(restored))

    cmd = [os.path.join(bindir, 'QtToolBin.exe'), jsonp,
           '--ename', os.path.join(proj, 'ename.h'),
           '--excel', xls, '-o', outdir, '--no-script',
           '--run-resbuilder', os.path.join(bindir, 'ResBuilder.exe')]
    r = subprocess.run(cmd, capture_output=True)
    if r.returncode != 0:
        restore()
        sys.stdout.write(r.stdout.decode('gbk', 'replace'))
        sys.stdout.write(r.stderr.decode('gbk', 'replace'))
        print('工具链返回 %d' % r.returncode)
        return 1

    print('%-24s %-8s %-8s %-8s %s' % ('文件', '本版', '原厂', '差字节', '判定'))
    bad = 0
    for n in FILES:
        pa = os.path.join(outdir, n)
        if not os.path.exists(pa) or n not in snapshot:
            print('%-24s %s' % (n, '缺文件'))
            bad += 1
            continue
        # 参考一律取快照 —— 磁盘上那份这会儿已经被本次运行顶掉了
        a, b = open(pa, 'rb').read(), snapshot[n]
        d = (sum(1 for i in range(min(len(a), len(b))) if a[i] != b[i])
             + abs(len(a) - len(b)))

        # 【判定看的是差在哪儿，不是差多少】
        # 旧判定是"差异字节数 <= 预算"，只数个数不看位置 —— 真出内容 bug，
        # 只要碰的字节数没超预算就照样绿。而且它确实掩盖过：工程目录里的
        # result.bin 参考被别的工程顶掉（10953 -> 11451），旧判定报"允许范围内"。
        unexplained = []
        if n == 'project.bin':
            for s0, p, xa, xb in explain_sty(a, b):
                unexplained.append('@0x%06X..0x%06X 本版=%s 原厂=%s' % (s0, p, xa, xb))
        elif n in ('result.bin', 'result.str'):
            for s0, p, xa, xb in explain_res(a, b):
                unexplained.append('@0x%06X..0x%06X 本版=%s 原厂=%s' % (s0, p, xa, xb))
        elif len(a) != len(b):
            unexplained.append('长度就不一样：本版 %d / 原厂 %d' % (len(a), len(b)))
        else:
            for ln, xa, xb in explain_text(n, a, b):
                unexplained.append('第 %d 行  本版=%s  原厂=%s' % (ln, xa, xb))

        ok = not unexplained
        print('%-24s %-8d %-8d %-8d %s' % (n, len(a), len(b), d,
              '逐字节相同' if d == 0 else ('差异都可解释' if ok else '★ 有讲不通的差异')))
        if not ok:
            bad += 1
            for line in unexplained[:6]:
                print('    %s' % line)
            if len(unexplained) > 6:
                print('    ...还有 %d 处' % (len(unexplained) - 6))
    restore()            # 工程目录一个字节都不留下改动
    print('\n不合格 %d 项' % bad)
    if bad:
        # 参考产物是"就地"躺在工程目录里的，任何一次 step2 都会覆盖它们。
        # 只有一两个文件对不上、其余全过时，八成是参考被别的工程覆盖过 ——
        # 把生成时间列出来，一眼能看出哪几个是后来被换掉的。
        print('\n参考产物（工程目录里那份）的生成时间：')
        for n in FILES:
            pb = os.path.join(proj, n)
            if os.path.exists(pb):
                t = datetime.datetime.fromtimestamp(os.path.getmtime(pb))
                print('  %-24s %s' % (n, t.strftime('%Y-%m-%d %H:%M:%S')))
        print('时间明显不一致 = 有人用别的工程跑过 step2 把参考覆盖了，'
              '不是本版工具变差了。')
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
