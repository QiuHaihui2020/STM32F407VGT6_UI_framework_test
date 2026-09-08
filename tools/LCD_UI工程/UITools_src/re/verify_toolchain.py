# -*- coding: utf-8 -*-
"""整链验收：用重建版 QtToolBin + ResBuilder 跑一遍，和原厂产物逐字节对比。

    python verify_toolchain.py <重建版 bin 目录> <工程目录> [输出目录]

例：
    python verify_toolchain.py C:\\bt\\uitools ..\\..\\ui_128_64_JL02\\模式界面\\project C:\\bt\\chain

会打印一张表，列出每个产物"差多少字节、差在哪"。已知且**不可能消除**的差异：

  project.bin  Time/number 控件的 char format[16] 尾部是原厂没初始化的栈/堆内存
               （四个控件各不相同的指针值），原厂自己重跑也不一样
  result.bin   4 字节 resver（原厂是随机值，空资源文件里也非零）
               + 调色板前缀的**排列顺序**（集合一致；OSD1 单色屏根本不用调色板）
  result.str   4 字节 resver
  res_ver.h / result_*_index.h   生成时间戳

除此之外应当**逐字节相同**。
"""
import os
import subprocess
import sys

FILES = [
    'project.bin', 'ename.h', 'result.bin', 'result.str', 'result.h',
    'res_ver.h', 'result_pic_index.h', 'result_str_index.h',
    'result.csv', 'result.xml',
]

# 每个文件允许的差异上限（字节），超了就算失败
BUDGET = {
    'project.bin': 96,          # 8 个 format[16] 尾部
    'result.bin': 4 + 1024 * 3,  # resver + 三页调色板
    'result.str': 4,
    'res_ver.h': 32,
    'result_pic_index.h': 8,
    'result_str_index.h': 8,
    'result.xml': 4096,
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
    jsonp = os.path.join(proj, jsons[0])
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
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
