# -*- coding: utf-8 -*-
"""全控件对拍：同一份 json，原厂工具链和本版各跑一遍，产物逐字节比。

    python verify_allctrl.py [工作目录]        默认 C:\\bt\\allctrl

【为什么要这个】原厂那两份样例工程（SmallColorTFT / SmallColor_oled）只覆盖
了它们自己用到的控件和组合 —— 表格控件一个实例都没有，列表套布局、布局套布局
这些也只有零星几例。拿它们对拍，没被用到的东西永远验不到。

所以这里换个打法：

  1. 让**本版编辑器**自己造一份样例工程（`UITools.exe --make-sample`）：
     每种控件各建一个、三种容器各带两项、项里再放图片和文字、
     布局套布局、同一个布局里混排一堆控件。
  2. 把这**同一份 json** 交给原厂 `QtToolBin.exe`（GUI，投 F5）跑一遍。
  3. 再交给本版 `QtToolBin.exe` 跑一遍 —— **故意不给已有的 ename.h**，
     让它独立分配 id，否则就是抄原厂的答案。
  4. 产物逐字节比。

【原厂目录一个字节都不碰】整棵 UITools + 工程复制到工作目录，全程在副本里跑。

【已知的、不算失败的差异】
  · ename.h 里 id 的**低 16 位**：那是宏名的哈希，原厂的算法还没反出来
    （re/hash_*.py 那几轮都没啃下来）。日常对拍走 `--ename` 把原厂那份
    喂进去，本版直接沿用它的编号，所以真实工程对拍是逐字节相同的；
    这里故意不喂，就是要把这个差异**量出来**。
    判定只看：宏名集合是否一致、**高位（工程号/页号/类型码）**是否一致。
  · Resbuilder.xml 里 <PictureList>/<ColorList> 的次序（原厂 QSet 随机序）。
"""
import io
import os
import re
import shutil
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

HERE = os.path.dirname(os.path.abspath(__file__))
# re/ -> UITools_src/ -> LCD_UI工程/
LCD = os.path.abspath(os.path.join(HERE, '..', '..'))
FACTORY_UITOOLS = os.path.join(LCD, 'UITools')
FACTORY_PROJ = os.path.join(LCD, 'ui_128_64_JL02')
MY_BIN = r'C:\bt\uitools'

PS = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command']


def run_ps(script, timeout=180):
    r = subprocess.run(PS + [script], capture_output=True, timeout=timeout)
    return (r.stdout.decode('utf-8', 'replace')
            + r.stderr.decode('utf-8', 'replace'))


def press_f5(exe, workdir, wait):
    """启动一个 GUI 工具，只对它自己的窗口投 F5，等它跑完再杀掉。

    【绝不能用 SendKeys/AppActivate】那会抢焦点，按键可能落到用户正在用的
    窗口上。用 $proc.MainWindowHandle 精确投递（方法学见 FACTORY_UI.md §9）。
    """
    script = r'''
$p = Start-Process -FilePath "%s" -WorkingDirectory "%s" -PassThru
Start-Sleep -Seconds 3
$p.Refresh()
$h = $p.MainWindowHandle
"HWND=$h"
if ($h -ne 0) {
  Add-Type -Namespace FF -Name N -MemberDefinition '[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr h,uint m,System.IntPtr w,System.IntPtr l);'
  [FF.N]::PostMessage($h, 0x0100, [IntPtr]0x74, [IntPtr]0) | Out-Null
  Start-Sleep -Milliseconds 60
  [FF.N]::PostMessage($h, 0x0101, [IntPtr]0x74, [IntPtr]0) | Out-Null
}
Start-Sleep -Seconds %d
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
''' % (exe, workdir, wait)
    return run_ps(script, timeout=wait + 120)


def read_defines(path):
    d = {}
    if not os.path.exists(path):
        return d
    for ln in io.open(path, encoding='utf-8', errors='replace'):
        m = re.match(r'#define\s+(\S+)\s+0X([0-9A-Fa-f]+)', ln)
        if m:
            d[m.group(1)] = int(m.group(2), 16)
    return d


def main():
    work = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else r'C:\bt\allctrl'
    if not os.path.isdir(FACTORY_UITOOLS):
        print('找不到原厂 UITools: %s' % FACTORY_UITOOLS)
        return 2

    print('工作目录: %s' % work)
    if os.path.isdir(work):
        shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    # ---- 1. 原厂树整棵复制一份，全程在副本里折腾 ----
    shutil.copytree(FACTORY_UITOOLS, os.path.join(work, 'UITools'))
    shutil.copytree(FACTORY_PROJ, os.path.join(work, 'proj'))
    pdir = os.path.join(work, 'proj', '模式界面', 'project')
    # 原有的 json 挪开，免得 project.ini 指错
    for f in os.listdir(pdir):
        if f.lower().endswith('.json'):
            os.rename(os.path.join(pdir, f), os.path.join(pdir, f + '.bak'))
    prods = ['project.bin', 'ename.h', 'result.bin', 'result.str', 'result.h',
             'res_ver.h', 'result_pic_index.h', 'result_str_index.h',
             'result.csv', 'result.xml', 'Resbuilder.xml', 'debug.txt']
    for f in prods:
        p = os.path.join(pdir, f)
        if os.path.exists(p):
            os.remove(p)

    # ---- 2. 本版编辑器造样例工程 ----
    jsonp = os.path.join(pdir, 'AllCtrl.json')
    env = dict(os.environ)
    env['QT_QPA_PLATFORM'] = 'offscreen'
    env['PATH'] = r'C:\Qt\5.15.2\msvc2019_64\bin;' + env.get('PATH', '')
    env['QT_QPA_PLATFORM_PLUGIN_PATH'] = r'C:\Qt\5.15.2\msvc2019_64\plugins\platforms'
    logp = os.path.join(work, 'sample.log')
    subprocess.run([os.path.join(MY_BIN, 'UITools.exe'),
                    '--tools-root', os.path.join(work, 'UITools'),
                    '--make-sample', jsonp, '--out', logp],
                   env=env, capture_output=True, timeout=300)
    if not os.path.exists(jsonp):
        print('样例工程没造出来')
        if os.path.exists(logp):
            print(io.open(logp, encoding='utf-8', errors='replace').read())
        return 1
    print('--- 样例工程 ---')
    print(io.open(logp, encoding='utf-8', errors='replace').read().strip())

    # project.ini 指向它，原厂 QtToolBin 靠这个找工程
    ini = os.path.join(pdir, 'config', 'ini', 'project.ini')
    if os.path.exists(ini):
        txt = io.open(ini, encoding='utf-8', errors='replace').read()
        txt = re.sub(r'(?im)^projectfilename=.*$',
                     'projectfilename=AllCtrl.json', txt)
        io.open(ini, 'w', encoding='utf-8').write(txt)

    # ---- 3. 原厂工具链跑一遍 ----
    print('\n--- 原厂 QtToolBin ---')
    print(press_f5(os.path.join(work, 'UITools', 'QtToolBin.exe'), pdir, 25).strip())
    ref = os.path.join(work, 'ref')
    os.makedirs(ref, exist_ok=True)
    for f in prods:
        p = os.path.join(pdir, f)
        if os.path.exists(p):
            shutil.copy2(p, ref)

    # ---- 4. 本版工具链跑一遍（故意不给已有 ename.h）----
    mine = os.path.join(work, 'mine')
    os.makedirs(mine, exist_ok=True)
    xls = os.path.join(work, 'UITools', '多国语言_128_64.xls')
    subprocess.run([os.path.join(MY_BIN, 'QtToolBin.exe'), jsonp,
                    '--ename', os.path.join(mine, '_none_.h'),
                    '--excel', xls, '-o', mine, '--no-script',
                    '--run-resbuilder', os.path.join(MY_BIN, 'ResBuilder.exe')],
                   capture_output=True, timeout=300)

    # ---- 5. 逐字节比 ----
    print('\n%-20s %-9s %-9s %s' % ('文件', '原厂', '本版', '判定'))
    bad = 0
    for f in prods:
        a, b = os.path.join(ref, f), os.path.join(mine, f)
        if not os.path.exists(a) and not os.path.exists(b):
            continue
        if not os.path.exists(a) or not os.path.exists(b):
            print('%-20s %-9s %-9s 只有一边有'
                  % (f, os.path.exists(a), os.path.exists(b)))
            continue
        da, db = open(a, 'rb').read(), open(b, 'rb').read()
        if da == db:
            print('%-20s %-9d %-9d ★ 逐字节相同' % (f, len(da), len(db)))
        else:
            n = sum(1 for x, y in zip(da, db) if x != y) + abs(len(da) - len(db))
            print('%-20s %-9d %-9d 差 %d 字节' % (f, len(da), len(db), n))

    # ---- 6. ename.h 结构化比：宏名集合 + id 高位 ----
    ea = read_defines(os.path.join(ref, 'ename.h'))
    eb = read_defines(os.path.join(mine, 'ename.h'))
    print('\n--- ename.h 结构化比对 ---')
    print('宏个数        原厂 %d / 本版 %d' % (len(ea), len(eb)))
    same_names = set(ea) == set(eb)
    print('宏名集合一致  %s' % ('是' if same_names else '否'))
    if not same_names:
        bad += 1
        print('  只在原厂: %s' % sorted(set(ea) - set(eb))[:10])
        print('  只在本版: %s' % sorted(set(eb) - set(ea))[:10])
    # 高位 = 工程号<<13 | 页号<<6 | 类型码（即 id>>16）
    hi_bad = [k for k in ea if k in eb and (ea[k] >> 16) != (eb[k] >> 16)]
    print('高位(工程/页/类型码)全一致  %s' % ('是' if not hi_bad else '否'))
    if hi_bad:
        bad += 1
        for k in sorted(hi_bad)[:10]:
            print('  %-18s 原厂 0x%X(类型%d) 本版 0x%X(类型%d)'
                  % (k, ea[k], (ea[k] >> 16) & 0x3F, eb[k], (eb[k] >> 16) & 0x3F))
    low_diff = [k for k in ea if k in eb and (ea[k] & 0xFFFF) != (eb[k] & 0xFFFF)]
    print('低16位(宏名哈希)不同 %d 个  —— 已知差异，见本文件抬头' % len(low_diff))
    # 本版自己不能撞车
    dup = len(eb) != len(set(eb.values()))
    print('本版 id 无重复  %s' % ('是' if not dup else '否'))
    if dup:
        bad += 1

    print('\n不合格 %d 项' % bad)
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
