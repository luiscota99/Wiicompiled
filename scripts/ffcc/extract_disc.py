#!/usr/bin/env python3
"""Extract a GameCube disc image into the folder layout the runtime reads (the same layout Dolphin's
"Extract Entire Disc" produces): <out>/sys/{boot.bin, bi2.bin, apploader.img, main.dol, fst.bin} and
<out>/files/... for the file system tree.

    python scripts/ffcc/extract_disc.py <game.iso> <out dir> [--dol projects/ffcc/main.dol]

Only plain GameCube images (.iso/.gcm, 1.4 GB) are handled; convert compressed images (RVZ, GCZ, CISO)
back to ISO with Dolphin first. Nothing is uploaded or modified; the image is only read.
"""
import argparse, os, struct, sys

def u32(b, o): return struct.unpack(">I", b[o:o + 4])[0]

def dol_size(f, dol_off):
    f.seek(dol_off); h = f.read(0x100)
    end = 0
    for i in range(18):   # 7 text + 11 data sections: file offsets at 0x00.., sizes at 0x90..
        off = u32(h, i * 4); size = u32(h, 0x90 + i * 4)
        if off and size: end = max(end, off + size)
    return end

def copy_range(f, off, size, dst, chunk=1 << 20):
    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    f.seek(off)
    with open(dst, "wb") as o:
        left = size
        while left > 0:
            b = f.read(min(chunk, left))
            if not b: raise IOError("image truncated at 0x%X" % f.tell())
            o.write(b); left -= len(b)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("iso"); ap.add_argument("out"); ap.add_argument("--dol", help="also copy main.dol to this path (e.g. projects/ffcc/main.dol)")
    a = ap.parse_args()
    with open(a.iso, "rb") as f:
        boot = f.read(0x440)
        if len(boot) < 0x440 or boot[0x1C:0x20] != b"\xC2\x33\x9F\x3D":
            print("not a GameCube disc image (magic missing); compressed images must be converted to ISO first"); return 2
        game_id = boot[:6].decode("ascii", "replace"); title = boot[0x20:0x400].split(b"\0", 1)[0].decode("ascii", "replace")
        dol_off, fst_off, fst_size = u32(boot, 0x420), u32(boot, 0x424), u32(boot, 0x428)
        print("disc %s: %s  dol@0x%X fst@0x%X (%d bytes)" % (game_id, title, dol_off, fst_off, fst_size))
        sysdir = os.path.join(a.out, "sys"); os.makedirs(sysdir, exist_ok=True)
        copy_range(f, 0, 0x440, os.path.join(sysdir, "boot.bin"))
        copy_range(f, 0x440, 0x2000, os.path.join(sysdir, "bi2.bin"))
        f.seek(0x2440 + 0x14); apl_size = u32(f.read(4), 0); f.seek(0x2440 + 0x18); apl_trailer = u32(f.read(4), 0)
        copy_range(f, 0x2440, 0x20 + apl_size + apl_trailer, os.path.join(sysdir, "apploader.img"))
        dsize = dol_size(f, dol_off); copy_range(f, dol_off, dsize, os.path.join(sysdir, "main.dol"))
        copy_range(f, fst_off, fst_size, os.path.join(sysdir, "fst.bin"))
        f.seek(fst_off); fst = f.read(fst_size)
        count = u32(fst, 8); names = fst[count * 12:]
        def name(e):
            o = u32(fst, e * 12) & 0x00FFFFFF; return names[o:names.index(b"\0", o)].decode("shift_jis", "replace")
        # walk: directory entries hold the index of the next sibling; files hold offset/size
        files = 0; stack = [(count, a.out and os.path.join(a.out, "files"))]
        i = 1
        while i < count:
            while stack and i >= stack[-1][0]: stack.pop()
            e = i * 12; is_dir = fst[e] == 1; parent_dir = stack[-1][1]
            if is_dir:
                nxt = u32(fst, e + 8); stack.append((nxt, os.path.join(parent_dir, name(i)))); os.makedirs(stack[-1][1], exist_ok=True); i += 1
            else:
                copy_range(f, u32(fst, e + 4), u32(fst, e + 8), os.path.join(parent_dir, name(i))); files += 1; i += 1
        print("extracted %d files, main.dol %d bytes -> %s" % (files, dsize, a.out))
        if a.dol:
            os.makedirs(os.path.dirname(a.dol) or ".", exist_ok=True)
            with open(os.path.join(sysdir, "main.dol"), "rb") as s, open(a.dol, "wb") as d: d.write(s.read())
            print("main.dol copied to", a.dol)
    return 0

if __name__ == "__main__":
    sys.exit(main())
