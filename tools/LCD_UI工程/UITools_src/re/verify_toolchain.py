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

    cmd = [os.path.join(bindir, 'QtToolBin.exe'), jsonp,
           '--ename', os.path.join(proj, 'ename.h'),
           '--excel', xls, '-o', outdir, '--no-script',
           '--run-resbuilder', os.path.join(bindir, 'ResBuilder.exe')]
    r = subprocess.run(cmd, capture_output=True)
    if r.returncode != 0:
        sys.stdout.write(r.stdout.decode('gbk', 'replace'))
        sys.stdout.write(r.stderr.decode('gbk', 'replace'))
        print('工具链返回 %d' % r.returncode)
        return 1

    print('%-24s %-8s %-8s %-8s %s' % ('文件', '本版', '原厂', '差字节', '判定'))
    bad = 0
    for n in FILES:
        pa, pb = os.path.join(outdir, n), os.path.join(proj, n)
        if not os.path.exists(pa) or not os.path.exists(pb):
            print('%-24s %s' % (n, '缺文件'))
            bad += 1
            continue
        a, b = open(pa, 'rb').read(), open(pb, 'rb').read()
        d = sum(1 for i in range(min(len(a), len(b))) if a[i] != b[i]) + abs(len(a) - len(b))
        budget = BUDGET.get(n, 0)
        ok = d <= budget
        print('%-24s %-8d %-8d %-8d %s' % (n, len(a), len(b), d,
              '逐字节相同' if d == 0 else ('允许范围内' if ok else '★ 超出预期')))
        if not ok:
            bad += 1
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
