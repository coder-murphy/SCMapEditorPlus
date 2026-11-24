# ScmDraft2 Original Editor Unpacking

## Source binary

- Path on this machine: `F:\Games\单机游戏\starcraft\[CN]ScmDraft2.exe`
- Size: 1 289 216 B
- SHA-256: `7B17D5AA71B9024438A54963870561DB0854270567A170FF9900CDE772B7802D`
- Version: 0.8.0.0, OriginalFilename `Scmdraft.exe`
- PE32, MSVC C++ / Win32, statically linked CRT, no packer, no MFC/Qt/.NET
- PDB path visible: `s:\ScmDraft 2.0 B\Release\ScmDraft 2.0 B.pdb` (symbols not shipped)

If the CN exe is unavailable, a different 0.8.0.0 build is acceptable for structure, but
re-verify resource IDs before trusting the dump.

## Dump command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\dump_original.ps1
```

Output goes to `reverse/scmdraft2/`:

| Path | Content |
| --- | --- |
| `raw/<TYPE>/<NAME>_<LANG>.bin` | Raw PE resource bytes. Custom type `BIN` goes to `raw/type_BIN/` so `.gitignore`'s `bin/` rule does not hide it. |
| `converted/bitmap/*.bmp` | DIB payloads with a 14-byte BMP header prepended |
| `converted/icon/*.ico` | RT_ICON / RT_GROUP_ICON merged |
| `converted/cursor/*.cur` | Cursor groups with hotspot |
| `converted/string/*.txt` | String tables, 16 entries per block, `id<TAB>text` |
| `converted/version/*.txt` | Readable version strings |
| `converted/manifest/*.xml` | Manifests |
| `resources.csv` | Type / name / language / size / path for all 42 resources |

The dump script must copy **named resource type strings** into stable unmanaged memory
before enumerating; pointers returned by the enumeration callback are only valid during
the callback.

## Resource inventory (VERIFIED)

| Type | Count | IDs | Total bytes |
| --- | --- | --- | --- |
| DIALOG | 25 | 103,155,163,164,166,167,169,171,172,173,174,175,179,180,181,182,183,184,185,186,187,188,189,191,192 | 18 698 |
| MENU | 1 | 109 | 8 888 |
| ACCELERATOR | 4 | 109,176,177,178 | 392 |
| STRING | 2 | 7,8 | 1 076 |
| BITMAP | 3 | 134,151,152 | 499 468 |
| ICON | 1 | — | 3 240 |
| GROUP_ICON | 1 | 130 | 20 |
| VERSION | 1 | 1 | 964 |
| Custom `BIN` | 4 | 148,170,183,193 | 36 793 |

The readable UI sources live in `src/res/` (`dialogs.rc`, `menu.rc`, `accel.rc`), not in
the dump directory.

## Custom BIN payloads (VERIFIED)

| ID | Size | Purpose |
| --- | --- | --- |
| 148 | 1 040 B | Default `VCOD` section for new maps |
| 170 | 18 953 B | Unit/sprite name tree (u16 offset table + paths such as `Units/Zerg/Air Units/Scourge`, 249 names) |
| 183 | 7 200 B | Default `TRIG` with 3 triggers (2 400 B each) |
| 193 | 9 600 B | Default `TRIG` with 4 triggers |

`src/res/assets/` contains `bin_148/170/183/193.bin`; `assets.rc` embeds 148/183/193 as
RCDATA with IDs `IDR_ASSET_VCOD/TRIG3/TRIG4`. `MapDocument::CreateNew` uses them.

## Bitmaps and icons

- `bitmap_134.bmp` 16x16 4bpp, `bitmap_151.bmp` 500x300, `bitmap_152.bmp` 128x128.
- The original toolbar icon strip is **not** in `RT_BITMAP`. The project reconstructs it
  from a screenshot into `src/res/assets/toolbar_icons.bmp` (11 icons, 16x16, magenta
  transparent) and `menu_icon.bmp` (menu banner icon). IDs are `IDB_TOOLBAR_ICONS` 207
  and `IDB_MENU_ICON` 208.

## Pitfalls

- `RT_BITMAP` bytes are DIB, not BMP; add the 14-byte file header.
- Icon/cursor groups must be merged from `RT_GROUP_ICON`/`RT_ICON`; cursor hotspots come
  from the group resource.
- Some BIN resources are only enumerable via the type name `BIN`; do not assume numeric
  types.
- Keep dumped files untouched; treat `reverse/scmdraft2/` as evidence.
- The CN `Local.dll` / `EditLocal.dll` are packed localization shims; use an unlocalized
  exe when doing dynamic analysis.
