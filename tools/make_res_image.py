#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
make_res_image.py - 把 PC 上的 UI 资源文件做成片内 Flash FATFS 磁盘镜像

流程:
  1. 解析 User/fs/flash_disk.h 里的 FLASH_DISK_BASE_ADDR / FLASH_DISK_SIZE /
     FLASH_DISK_SECTOR_SIZE (单一事实来源, 改宏后本脚本自动跟随);
  2. 把 tools/JL/*  打进镜像 /JL/ 目录, tools/font/* 打进 /font/ 目录
     (与 ui_port_config.h 约定的盘上布局 0:/JL/... 0:/font/... 一致);
  3. 生成 tools/res_image/fat_image.bin  (镜像本体, 可用 CubeProgrammer 单独烧)
     生成 User/fs/res_image.c            (.res_image 段 const 数组, 随固件下载);
  4. 生成后按 FAT 链回读, 与源文件逐字节比对校验.

镜像烧到 FLASH_DISK_BASE_ADDR 之后:
  - 上电 FatFs 直接挂载, 不再需要用 USB MSC 拷资源;
  - USB MSC / 运行期 FatFs 仍可正常读写这片 Flash (镜像只是初始内容);
  - 每次下载固件, 镜像都会恢复成编译时的资源版本.

用法:
  python tools/make_res_image.py            # 完整输出
  python tools/make_res_image.py --quiet    # 精简输出 (Keil Before Build 用这个)

增量: 资源文件没变化时不会重写 res_image.c, 不打断 Keil 增量编译.
"""

import os
import re
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)

FLASH_DISK_H = os.path.join(PROJECT_ROOT, "User", "fs", "flash_disk.h")
RES_IMAGE_C = os.path.join(PROJECT_ROOT, "User", "fs", "res_image.c")
IMAGE_DIR = os.path.join(SCRIPT_DIR, "res_image")
IMAGE_BIN = os.path.join(IMAGE_DIR, "fat_image.bin")

# (镜像内目录名, 源目录). 框架从 0:/JL/JL.res|JL.str|JL.sty 和
# 0:/font/ascii.res|F_GB2312.* 读资源, 目录名必须与 jl_res_config.h 对上.
SOURCE_DIRS = [
    ("JL", os.path.join(SCRIPT_DIR, "JL")),
    ("font", os.path.join(SCRIPT_DIR, "font")),
]

# 目录项时间戳(固定值, 保证镜像可复现): 2026-01-01 12:00:00
FIX_DATE = ((2026 - 1980) << 9) | (1 << 5) | 1
FIX_TIME = (12 << 11) | (0 << 5) | 0

ATTR_FILE = 0x20
ATTR_DIR = 0x10
ATTR_LFN = 0x0F

FAT12_MAX_CLUSTERS = 4084
EOC12 = 0x0FFF
EOC16 = 0xFFFF


def fail(msg):
    print("[make_res_image] ERROR: %s" % msg)
    sys.exit(1)


# ---------------------------------------------------------------- flash_disk.h

def parse_flash_disk_h(path):
    """从 flash_disk.h 提取磁盘区基地址/容量/逻辑扇区大小."""
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as fp:
            text = fp.read()
    except OSError as exc:
        fail("cannot read %s: %s" % (path, exc))

    def get(name):
        m = re.search(r"#define\s+%s\s+\((.*?)\)" % name, text, re.S)
        if not m:
            fail("%s not found in %s" % (name, path))
        # "256UL * 1024UL" -> "256 * 1024"
        expr = re.sub(r"(?<=\d)[ULul]+", "", m.group(1))
        try:
            return int(eval(expr, {"__builtins__": {}}, {}))
        except Exception:
            fail("cannot evaluate %s = %s" % (name, expr))

    base = get("FLASH_DISK_BASE_ADDR")
    size = get("FLASH_DISK_SIZE")
    sector = get("FLASH_DISK_SECTOR_SIZE")

    if sector not in (512, 1024, 2048, 4096):
        fail("FLASH_DISK_SECTOR_SIZE = %d, FatFs 只支持 512/1024/2048/4096" % sector)
    if size % sector != 0:
        fail("FLASH_DISK_SIZE (%d) 不是逻辑扇区 (%d) 的整数倍" % (size, sector))
    if base % sector != 0:
        fail("FLASH_DISK_BASE_ADDR (0x%08X) 未对齐到逻辑扇区" % base)
    return base, size, sector


# ---------------------------------------------------------------- 8.3 文件名

BAD_83_CHARS = set('*?"<>|\\/:;+=,[] ')


def make_83(name):
    """文件名转 8.3 短名 11 字节 (不含 LFN; 工程 _USE_LFN=0)."""
    upper = name.upper()
    stem, dot, ext = upper.rpartition(".")
    if not dot:
        stem, ext = upper, ""
    if not (1 <= len(stem) <= 8) or len(ext) > 3:
        fail("文件名 %r 不符合 8.3 命名 (_USE_LFN=0), 请重命名资源文件" % name)
    for ch in stem + ext:
        if ch in BAD_83_CHARS or ord(ch) < 0x20 or ord(ch) > 0x7E:
            fail("文件名 %r 含 8.3 非法字符 %r" % (name, ch))
    return (stem.ljust(8) + ext.ljust(3)).encode("ascii")


def dir_entry(name83, attr, first_cluster, file_size):
    """32 字节 FAT 目录项."""
    e = bytearray(32)
    e[0:11] = name83
    e[11] = attr
    e[12] = 0                    # NTRes: 全大写
    e[13] = 0                    # CrtTimeTenth
    e[14:16] = FIX_TIME.to_bytes(2, "little")     # CrtTime
    e[16:18] = FIX_DATE.to_bytes(2, "little")     # CrtDate
    e[18:20] = FIX_DATE.to_bytes(2, "little")     # LastAccDate
    e[20:22] = (0).to_bytes(2, "little")          # FirstClusHI (FAT12/16 恒 0)
    e[22:24] = FIX_TIME.to_bytes(2, "little")     # WrtTime
    e[24:26] = FIX_DATE.to_bytes(2, "little")     # WrtDate
    e[26:28] = first_cluster.to_bytes(2, "little")
    e[28:32] = file_size.to_bytes(4, "little")
    return bytes(e)


# ---------------------------------------------------------------- FAT 表

class FatTable(object):
    def __init__(self, count, ftype):
        self.count = count
        self.ftype = ftype
        bits = count * 12 if ftype == 12 else count * 16
        self.data = bytearray((bits + 7) // 8)

    def set(self, idx, val):
        d = self.data
        if self.ftype == 12:
            off = (idx * 3) // 2
            if idx & 1:
                d[off] = (d[off] & 0x0F) | ((val & 0x0F) << 4)
                d[off + 1] = (val >> 4) & 0xFF
            else:
                d[off] = val & 0xFF
                d[off + 1] = (d[off + 1] & 0xF0) | ((val >> 8) & 0x0F)
        else:
            d[idx * 2] = val & 0xFF
            d[idx * 2 + 1] = (val >> 8) & 0xFF

    def get(self, idx):
        d = self.data
        if self.ftype == 12:
            off = (idx * 3) // 2
            if idx & 1:
                return (d[off] >> 4) | (d[off + 1] << 4)
            return d[off] | ((d[off + 1] & 0x0F) << 8)
        return d[idx * 2] | (d[idx * 2 + 1] << 8)


# ---------------------------------------------------------------- 布局

def plan_layout(total_sectors, sector, spc, n_root_items):
    """给定每簇扇区数, 解出 FAT 表大小(定点迭代). 不够放则返回 None."""
    reserved = 1
    nfats = 2
    root_entries = max(16, ((n_root_items + 4 + 15) // 16) * 16)
    root_sectors = (root_entries * 32 + sector - 1) // sector

    fat_size = 1
    for _ in range(64):
        data_sectors = total_sectors - reserved - nfats * fat_size - root_sectors
        clusters = data_sectors // spc
        if clusters < 1:
            return None
        ftype = 12 if clusters <= FAT12_MAX_CLUSTERS else 16
        fat_bits = (clusters + 2) * (12 if ftype == 12 else 16)
        need = ((fat_bits + 7) // 8 + sector - 1) // sector
        if need <= fat_size:
            return {
                "spc": spc, "reserved": reserved, "nfats": nfats,
                "root_entries": root_entries, "root_sectors": root_sectors,
                "fat_size": fat_size, "fat_type": ftype,
                "clusters": clusters,
                "data_start": reserved + nfats * fat_size + root_sectors,
            }
        fat_size = need
        if reserved + nfats * fat_size + root_sectors >= total_sectors:
            return None
    return None


def build_boot_sector(tot_sec, sector, spc, reserved, nfats,
                      root_entries, fat_size, ftype):
    bs = bytearray(sector)
    bs[0:3] = b"\xEB\x3C\x90"                       # JumpBoot
    bs[3:11] = b"MSDOS5.0"                          # OEM Name
    bs[11:13] = sector.to_bytes(2, "little")        # BPB_BytsPerSec
    bs[13] = spc                                    # BPB_SecPerClus
    bs[14:16] = reserved.to_bytes(2, "little")      # BPB_RsvdSecCnt
    bs[16] = nfats                                  # BPB_NumFATs
    bs[17:19] = root_entries.to_bytes(2, "little")  # BPB_RootEntCnt
    if tot_sec < 0x10000:
        bs[19:21] = tot_sec.to_bytes(2, "little")   # BPB_TotSec16
    else:
        bs[32:36] = tot_sec.to_bytes(4, "little")   # BPB_TotSec32
    bs[21] = 0xF8                                   # BPB_Media
    bs[22:24] = fat_size.to_bytes(2, "little")      # BPB_FATSz16
    bs[24:26] = (32).to_bytes(2, "little")          # BPB_SecPerTrk
    bs[26:28] = (8).to_bytes(2, "little")           # BPB_NumHeads
    bs[28:32] = (0).to_bytes(4, "little")           # BPB_HiddSec
    bs[36] = 0x80                                   # BS_DrvNum
    bs[38] = 0x29                                   # BS_BootSig
    bs[39:43] = (0x20260101).to_bytes(4, "little")  # BS_VolID
    bs[43:54] = b"NO NAME    "                      # BS_VolLab
    bs[54:62] = b"FAT12   " if ftype == 12 else b"FAT16   "
    bs[510] = 0x55
    bs[511] = 0xAA
    return bs


def build_image(size, sector, src_dirs):
    """构建磁盘镜像, 返回 (镜像 bytes, 布局信息, 文件清单)."""
    total_sectors = size // sector

    # 选每簇扇区数: 从 1 开始, 找第一个装得下的
    n_root_items = len(src_dirs)
    layout = None
    for spc in (1, 2, 4, 8, 16, 32, 64, 128):
        layout = plan_layout(total_sectors, sector, spc, n_root_items)
        if layout:
            break
    if not layout:
        fail("磁盘容量 %d 字节太小, 放不下目录结构" % size)

    spc = layout["spc"]
    cluster_bytes = spc * sector
    clusters = layout["clusters"]
    ftype = layout["fat_type"]
    eoc = EOC12 if ftype == 12 else EOC16

    fat = FatTable(clusters + 2, ftype)
    fat.set(0, 0xFFF8 if ftype == 16 else 0xFF8)    # media
    fat.set(1, eoc)                                  # EOC 标记

    next_free = [2]

    def alloc(n, what):
        first = next_free[0]
        if first + n - 2 > clusters:
            fail("%s 需要的簇超出磁盘容量 (已用 %d/%d 簇), "
                 "请增大 flash_disk.h 的 FLASH_DISK_SIZE" % (what, first - 2, clusters))
        next_free[0] += n
        return first

    img = bytearray(b"\xFF" * size)   # 未用区域保持 0xFF (等价于擦除态)

    # 注意: 引导扇区/FAT 表/根目录在所有簇链建好之后再写入, 保证 FAT 里的
    # 链表信息完整; 这里先把根目录项收集到列表, 最后统一落盘.
    root_entries_buf = []
    root_used = [0]

    def put_root(entry):
        if root_used[0] >= layout["root_entries"]:
            fail("根目录项不足 (需 %d 项)" % (root_used[0] + 1))
        root_entries_buf.append(entry)
        root_used[0] += 1

    def data_offset(cluster):
        return layout["data_start"] * sector + (cluster - 2) * cluster_bytes

    # --- 子目录 + 文件 ---
    listing = []
    for dname, files in src_dirs:
        dcl = alloc(1, "目录 /%s" % dname)
        d_off = data_offset(dcl)
        img[d_off:d_off + cluster_bytes] = b"\x00" * cluster_bytes

        content = bytearray()
        content += dir_entry(b".          ", ATTR_DIR, dcl, 0)
        content += dir_entry(b"..         ", ATTR_DIR, 0, 0)   # 父目录 = 根

        for fname, fpath, fsize in files:
            data = open(fpath, "rb").read()
            if len(data) != fsize:
                fail("read %s: size changed" % fpath)
            name83 = make_83(fname)
            n_clus = max(1, (fsize + cluster_bytes - 1) // cluster_bytes)
            fcl = alloc(n_clus, "文件 /%s/%s" % (dname, fname))
            for i in range(n_clus - 1):
                fat.set(fcl + i, fcl + i + 1)
            fat.set(fcl + n_clus - 1, eoc)

            f_off = data_offset(fcl)
            img[f_off:f_off + fsize] = data
            tail = n_clus * cluster_bytes - fsize
            if tail:
                img[f_off + fsize:f_off + n_clus * cluster_bytes] = b"\x00" * tail

            content += dir_entry(name83, ATTR_FILE, fcl, fsize)
            listing.append(("/%s/%s" % (dname, fname), fsize, n_clus))

        if len(content) > cluster_bytes:
            fail("目录 /%s 条目过多, 一个簇 (%d 字节) 放不下" % (dname, cluster_bytes))
        img[d_off:d_off + len(content)] = content
        put_root(dir_entry(make_83(dname), ATTR_DIR, dcl, 0))
        listing.insert(0, ("/%s" % dname, 0, 1))

    # --- 所有簇链建好之后, 落盘引导扇区 / FAT 表 / 根目录 ---
    img[0:sector] = build_boot_sector(
        total_sectors, sector, spc, layout["reserved"], layout["nfats"],
        layout["root_entries"], layout["fat_size"], ftype)

    # FAT 表: 空闲表项必须为 0x00, 所以用 0 补齐到整数个扇区
    fat_region = bytes(fat.data)
    fat_region += b"\x00" * (layout["fat_size"] * sector - len(fat_region))
    for f in range(layout["nfats"]):
        off = (layout["reserved"] + f * layout["fat_size"]) * sector
        img[off:off + len(fat_region)] = fat_region

    root_off = (layout["reserved"] + layout["nfats"] * layout["fat_size"]) * sector
    root_bytes = layout["root_sectors"] * sector
    img[root_off:root_off + root_bytes] = b"\x00" * root_bytes
    for i, entry in enumerate(root_entries_buf):
        img[root_off + i * 32:root_off + (i + 1) * 32] = entry

    info = dict(layout)
    info.update({
        "size": size, "sector": sector, "total_sectors": total_sectors,
        "used_clusters": next_free[0] - 2, "eoc": eoc,
        "data_start": layout["data_start"],
    })
    return img, info, listing


# ---------------------------------------------------------------- 回读校验

def verify_image(img, info, src_dirs):
    """按 FAT 链独立回读镜像, 与源文件逐字节比对."""
    sector = info["sector"]
    if int.from_bytes(img[11:13], "little") != sector:
        fail("verify: BytsPerSec mismatch")
    if img[510:512] != b"\x55\xAA":
        fail("verify: boot signature (0x55AA) missing")

    spc = img[13]
    reserved = int.from_bytes(img[14:16], "little")
    nfats = img[16]
    root_entries = int.from_bytes(img[17:19], "little")
    fatsz = int.from_bytes(img[22:24], "little")
    ftype = 12 if img[54:62].rstrip() == b"FAT12" else 16
    cluster_bytes = spc * sector
    fat_off = reserved * sector
    root_off = (reserved + nfats * fatsz) * sector
    data_off = (reserved + nfats * fatsz + (root_entries * 32 + sector - 1) // sector) * sector
    eoc = EOC12 if ftype == 12 else EOC16

    def fat_get(idx):
        if ftype == 12:
            off = fat_off + (idx * 3) // 2
            if idx & 1:
                return (img[off] >> 4) | (img[off + 1] << 4)
            return img[off] | ((img[off + 1] & 0x0F) << 8)
        off = fat_off + idx * 2
        return img[off] | (img[off + 1] << 8)

    def parse_dir(buf):
        out = []
        for i in range(0, len(buf), 32):
            e = buf[i:i + 32]
            if len(e) < 32 or e[0] == 0x00:
                break
            if e[0] == 0xE5 or e[11] == ATTR_LFN:
                continue
            name = bytes(e[0:8]).decode("ascii").rstrip() \
                 + ("." + bytes(e[8:11]).decode("ascii").rstrip()
                    if bytes(e[8:11]).strip() else "")
            out.append((name, e[11],
                        int.from_bytes(e[26:28], "little"),
                        int.from_bytes(e[28:32], "little")))
        return out

    def read_file(first, size):
        out = bytearray()
        cl = first
        for _ in range(info["clusters"] + 2):     # 防簇环
            if not (2 <= cl < info["clusters"] + 2):
                fail("verify: FAT chain broken at cluster %d" % cl)
            off = data_off + (cl - 2) * cluster_bytes
            out += img[off:off + cluster_bytes]
            cl = fat_get(cl)
            if cl >= eoc:
                break
        return bytes(out[:size])

    root = parse_dir(bytes(img[root_off:root_off + root_entries * 32]))
    for name, attr, cl, _sz in root:
        if attr & ATTR_DIR:
            dcl = cl
            buf = img[data_off + (dcl - 2) * cluster_bytes:
                      data_off + (dcl - 1) * cluster_bytes]
            for fname, fattr, fcl, fsize in parse_dir(buf):
                if fattr & ATTR_DIR:
                    continue
                src = None
                for dname, files in src_dirs:
                    if dname.upper() == name.upper():
                        for fn, fp, fsz in files:
                            if fn.upper() == fname.upper():
                                src = (fp, fsz)
                if src is None:
                    fail("verify: unexpected file /%s/%s" % (name, fname))
                got = read_file(fcl, fsize)
                if fsize != src[1]:
                    fail("verify: /%s/%s size %d != %d" % (name, fname, fsize, src[1]))
                with open(src[0], "rb") as fp:
                    if got != fp.read():
                        fail("verify: /%s/%s content mismatch" % (name, fname))
    return True


# ---------------------------------------------------------------- 输出

C_HEADER = '''/**
  ******************************************************************************
  * @file    res_image.c
  * @brief   片内 Flash FATFS 磁盘镜像 (由 tools/make_res_image.py 生成, 勿手改)
  * @note    内容 = tools/JL 下文件 -> /JL/, tools/font 下文件 -> /font/ 的
  *          FAT12 磁盘镜像. .res_image 段由 MDK-ARM/FLASH_RELEASE 目录下的
  *          scatter 文件定位到 FLASH_DISK_BASE_ADDR, 随固件一起下载:
  *            - 上电 FatFs 直接挂载, 无需再用 USB MSC 拷资源;
  *            - USB MSC 或运行期 FatFs 仍可读写这片 Flash, 镜像只是初始内容;
  *            - 资源更新: 重跑脚本或直接重新编译 (Before Build 自动执行);
  *              内容没变时本文件不会被重写, 不影响增量编译.
  ******************************************************************************
  */

#include "flash_disk.h"

#if FLASH_DISK_EMBED_IMAGE

__attribute__((used, section(".res_image")))
const uint8_t g_res_image[FLASH_DISK_SIZE] = {
'''


def write_c_file(img, quiet):
    lines = [C_HEADER]
    for i in range(0, len(img), 16):
        lines.append("    " + ",".join("0x%02X" % b for b in img[i:i + 16]) + ",")
    lines.append("};")
    lines.append("")
    lines.append("#else /* 镜像未启用: 占位, 避免空编译单元 */")
    lines.append("")
    lines.append("typedef int res_image_disabled_placeholder_t;")
    lines.append("")
    lines.append("#endif /* FLASH_DISK_EMBED_IMAGE */")
    content = "\n".join(lines) + "\n"

    if os.path.exists(RES_IMAGE_C):
        with open(RES_IMAGE_C, "r", encoding="utf-8") as fp:
            if fp.read() == content:
                if not quiet:
                    print("  res_image.c unchanged, skip rewrite")
                return False
    with open(RES_IMAGE_C, "w", encoding="utf-8", newline="\n") as fp:
        fp.write(content)
    return True


def write_bin(img, quiet):
    os.makedirs(IMAGE_DIR, exist_ok=True)
    if os.path.exists(IMAGE_BIN):
        with open(IMAGE_BIN, "rb") as fp:
            if fp.read() == img:
                if not quiet:
                    print("  fat_image.bin unchanged, skip rewrite")
                return False
    with open(IMAGE_BIN, "wb") as fp:
        fp.write(img)
    return True


def main():
    quiet = "--quiet" in sys.argv
    base, size, sector = parse_flash_disk_h(FLASH_DISK_H)

    src_dirs = []
    for dname, dpath in SOURCE_DIRS:
        if not os.path.isdir(dpath):
            fail("源目录不存在: %s" % dpath)
        files = []
        for fn in sorted(os.listdir(dpath)):
            fp = os.path.join(dpath, fn)
            if os.path.isfile(fp):
                files.append((fn, fp, os.path.getsize(fp)))
        if not files:
            fail("源目录里没有文件: %s" % dpath)
        src_dirs.append((dname, files))

    img, info, listing = build_image(size, sector, src_dirs)
    verify_image(img, info, src_dirs)

    bin_changed = write_bin(img, quiet)
    c_changed = write_c_file(img, quiet)

    print("[make_res_image] FAT%d image @ 0x%08X..0x%08X (%d bytes, %d sectors, "
          "%d B/cluster)" % (info["fat_type"], base, base + size - 1, size,
                             info["total_sectors"], info["spc"] * sector))
    if not quiet:
        for path, fsize, n_clus in listing:
            if fsize:
                print("    %-24s %7d bytes  %3d clusters" % (path, fsize, n_clus))
            else:
                print("    %-24s <dir>" % path)
    print("    used %d/%d clusters (%.1f%%), free %d bytes" %
          (info["used_clusters"], info["clusters"],
           100.0 * info["used_clusters"] / info["clusters"],
           (info["clusters"] - info["used_clusters"]) * info["spc"] * sector))
    print("    verify OK: %d files byte-exact" %
          sum(1 for _, fs, _ in listing if fs))
    if bin_changed:
        print("    wrote %s" % os.path.relpath(IMAGE_BIN, PROJECT_ROOT))
    if c_changed:
        print("    wrote %s" % os.path.relpath(RES_IMAGE_C, PROJECT_ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
