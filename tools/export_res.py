#!/usr/bin/env python3
"""从原版 ScmDraft2.exe 导出位图与自定义 BIN 资源。

用法：
    python tools/export_res.py --exe "<星际安装目录>/[CN]ScmDraft2.exe"

导出的位图/BIN 已随仓库放在 src/res/assets/，仅在重新逆向时才需要游戏目录。

位图（RT_BITMAP）资源在 PE 中只存 DIB（BITMAPINFOHEADER + 调色板 + 像素），
脚本补一个 14 字节 BITMAPFILEHEADER 后写成标准 .bmp。
"""
import argparse
import os
import struct
import sys

try:
    import pefile
except ImportError:
    sys.exit("需要 pefile：python -m pip install pefile")

RT_BITMAP = 2


def dib_to_bmp(dib: bytes) -> bytes:
    if len(dib) < 40:
        raise ValueError("DIB 太短")
    bi_size = struct.unpack_from("<I", dib, 0)[0]
    bit_count = struct.unpack_from("<H", dib, 14)[0]
    colors_used = struct.unpack_from("<I", dib, 32)[0]
    palette = 0
    if bit_count <= 8:
        palette = (colors_used or (1 << bit_count)) * 4
    off_bits = 14 + bi_size + palette
    file_size = 14 + len(dib)
    header = b"BM" + struct.pack("<IHHI", file_size, 0, 0, off_bits)
    return header + dib


def iter_resources(pe, type_id):
    for entry in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if entry.id != type_id:
            continue
        for name_entry in entry.directory.entries:
            for lang_entry in name_entry.directory.entries:
                data = lang_entry.data.struct
                yield name_entry.id, pe.get_data(data.OffsetToData, data.Size)


def iter_custom_bin(pe):
    for entry in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if not entry.name or entry.name.string != b"BIN":
            continue
        for name_entry in entry.directory.entries:
            for lang_entry in name_entry.directory.entries:
                data = lang_entry.data.struct
                yield name_entry.id, pe.get_data(data.OffsetToData, data.Size)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True, help="原版 ScmDraft2.exe")
    ap.add_argument("--out", default=None, help="输出目录（默认 src/res/assets）")
    args = ap.parse_args()

    out_dir = args.out or os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                       "src", "res", "assets")
    os.makedirs(out_dir, exist_ok=True)
    pe = pefile.PE(args.exe)

    for rid, data in iter_resources(pe, RT_BITMAP):
        path = os.path.join(out_dir, f"bitmap_{rid}.bmp")
        with open(path, "wb") as f:
            f.write(dib_to_bmp(data))
        print(f"BITMAP {rid}: {len(data)} -> {path}")

    for rid, data in iter_custom_bin(pe):
        path = os.path.join(out_dir, f"bin_{rid}.bin")
        with open(path, "wb") as f:
            f.write(data)
        print(f"BIN {rid}: {len(data)} -> {path}")


if __name__ == "__main__":
    main()
