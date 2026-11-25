# StarCraft MPQ and Tilesets

## Two kinds of archives

| Kind | Examples | Use |
| --- | --- | --- |
| Map archive | `.scm`, `.scx` | Contains `(listfile)` and `staredit\scenario.chk` |
| Data archive | `StarDat.mpq`, `BrooDat.mpq` | Game data, including `arr\units.dat`, `rez\stat_txt.tbl`, tilesets |

Copies of the data archives live in `mpq/` so builds and tests do not need the game
install directory. The game directory on this machine is
`F:\Games\单机游戏\starcraft`.

## Map MPQ rules (VERIFIED)

| Rule | Value |
| --- | --- |
| Hash slot | hash type 0: `hashString(name, 0) & (hashSize - 1)`; not hashA |
| Locale | `0x0000`, not `0xFFFF` |
| `(listfile)` | Required |
| Compression | zlib `0x02` and PKWARE implode `0x08` |
| Sector table | Encrypted files: decrypt with `key-1`; unencrypted files have no sector table |
| Sector i | Decrypt with `key+i` |
| Sector table entry 0 | Equals the sector table's own byte length |

Using hash type 1 for the slot makes `SFileOpenArchive` succeed but
`SFileOpenFileEx("staredit\\scenario.chk")` fail; ScmDraft2 reports
"Error reading scenario.chk from MPQ".

## Key derivation

```
key = hashString(basename, 3)
if (flags & 0x00020000)            // FIX_KEY / adjusted
    key = ((key + blockPos) ^ fileSize)
```

The tileset files in `StarDat.mpq` / `BrooDat.mpq` all carry `0x00020000`; skipping the
`^ fileSize` step yields an invalid sector table.

## Compression details (both are easy to get wrong)

- zlib sectors start with `78 9C` (2 bytes). Detect with
  `(b0 & 0x0F) == 8 && ((b0 << 8 | b1) % 31) == 0`; retry raw deflate if it fails.
- PKWARE implode's distance table expands to **64** symbols using the
  `{2,20,53,230,247,151,248}` seeds (high nibble = repeat count - 1, low nibble = code
  length). A 7-entry cap misclassifies long-distance matches.

## Writer layout (used when saving maps)

```
header(32) | file data (16-byte aligned) | hash table (1024 × 16) | block table (n × 16)
```

- Default flags `0x80000000` (exists) → uncompressed, unencrypted, no sector table.
- Compressible entries (512 B ≤ size ≤ 16 MB) use DEFLATE only when at least 16 B smaller:
  flags `0x80000000 | 0x01000000 | 0x00000200`, payload `0x02` + raw deflate, no sector
  table.
- Hash slots use `hash(name, 0) & 1023` with linear probing; locale/platform are 0.
- Hash and block tables are encrypted with `hash("(hash table)",3)` and
  `hash("(block table)",3)`.
- Always include `(listfile)` and preserve extra files from the source map.

## Code and tools

| Item | Location |
| --- | --- |
| Reader/writer | `src/archive/mpq_archive.*` |
| Deflate/implode | `src/archive/mpq_codec.*` |
| Map CHK load/save | `src/map/map_document.*`, `src/chk/` |
| Tileset export | `src/terrain/tileset_export.*`, `scmd_cli extract-ts` |
| Map self-test | `SCMDTests.exe --mpq=<map>` (writes `%TEMP%\scmdplus_mpq.txt`) |
| Single entry extraction | `SCMDPlus.exe --mpq=<archive> --mpq-extract=<entry> --extract-to=<out-file>` |

PowerShell notes for the extraction mode (VERIFIED):

- Call it with `&` and quoted arguments, e.g.
  `& exe\SCMDPlus.exe --mpq="mpq\BrooDat.mpq" --mpq-extract="unit\neutral\geyser.grp" --extract-to="$env:TEMP\geyser.grp"`.
- Passing the same arguments through `Start-Process -ArgumentList @(...)` mangles the
  backslashes and the tool reports `extract=FAIL args`.
- `--extract-to` is a **file** path, not a directory.
- Unit GRPs are under `unit\<race>\...`; shadow GRPs are siblings such as
  `unit\neutral\min01Sha.grp` (see `objects.md`).

## Tilesets

`scmd_cli.exe extract-ts <game-dir> <out-dir>` reads `StarDat.mpq` and `BrooDat.mpq` and
writes 8 eras × 4 files (32 files, ~13.5 MB): `VX4`, `VR4`, `CV5`, `WPE`-style data plus
palette data. The repository keeps them in `ts/`; the build copies `ts/` next to both
EXEs. Tests accept `--ts=<dir>` and fall back to `exe\ts`, then `..\..\..\ts`.

Useful verified locations:

- `BrooDat.mpq` → `rez\stat_txt.tbl` (unit names; `name0=Terran Marine`)
- `BrooDat.mpq` → `arr\units.dat`
- Map archive → `staredit\scenario.chk` (case-insensitive alternatives handled by
  `ReadScenarioChk`)

## Verified samples

| Map | CHK size | Size |
| --- | --- | --- |
| (8)Space Circuit.scx (zlib) | 369 947 | 192×192 |
| (8)Astro World v1.3.scx (implode) | 391 875 | 192×192 |
| (8)Mangosteen v1.0.scx (implode) | 388 281 | 192×192 |
| (6)Neo China.scx (implode) | 161 973 | 192×64 |

Golden check for container reads: use 32-bit PowerShell with the game's
`Libs\SFmpq.dll` and call `SFileOpenArchive`, `SFileOpenFileEx`,
`SFileGetFileSize`, `SFileReadFile`. 64-bit processes cannot load that DLL.

## Not implemented

Compression types `0x01` (Huffman) and `0x10` (bzip2) are not present in StarCraft 1 maps;
leave them unsupported rather than guessing.
