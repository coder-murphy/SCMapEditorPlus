# Repository Resource Map

## Top-level layout

| Path | Content |
| --- | --- |
| `src/archive/` | MPQ reader/writer and codecs |
| `src/chk/` | CHK section container |
| `src/map/` | `MapDocument`, decor/fog, switches, edit history |
| `src/terrain/` | Tileset, renderer, brushes, edge placer, tileset export |
| `src/units/` | Unit records, `units.dat`, `stat_txt.tbl`, iscript/GRP |
| `src/triggers/` | Trigger, string, location models |
| `src/players/` | Player roster and upgrade/tech restrictions |
| `src/api/` | `SCMDCore` C ABI |
| `src/app/` | GUI application, settings, backups, UI probes |
| `src/ui/` | Main window, dialogs, palettes, command table, form/event layer |
| `src/tests/` | Headless test runner (`SCMDTests`) |
| `src/res/` | Dialogs, menu, accelerators, embedded assets |
| `reverse/scmdraft2/` | Original editor dump (read-only evidence) |
| `mpq/` | `StarDat.mpq`, `BrooDat.mpq` |
| `ts/` | 8 exported tilesets (32 files) + `stat_txt.tbl` (unit names for the scene tree) |
| `tools/` | Dump/regression/UI-audit scripts and repro maps |
| `doc/` | Verified format and UI documentation |

## Executables

| Binary | Purpose |
| --- | --- |
| `SCMDPlus.exe` | Win32 GUI editor; keeps only UI probes (`--unit-param-test`, `--clip-test`, `--doodad-test`, `--loc-draw-test`, `--brush-pattern-test`, `--menu-test`, `--menu-cmd-test`, `--view-test`, `--trigger-modal-test`, `--dialog-audit-test`) |
| `SCMDTests.exe` | Console headless tests (map/model/format/render); same `%TEMP%\scmdplus_mpq.txt` report |
| `SCMDCore.dll` | C ABI for external callers (`scmd_map_open`, tiles, units, render BMP, extract tilesets) |
| `scmd_cli.exe` | CLI example: `info`, `render`, `new`, `extract-ts` |

## `src/res/` resource files

| File | Content | ID ranges |
| --- | --- | --- |
| `resource.h` | All resource IDs | dialogs 103–206, assets 148/183/193, toolbar 207, menu icon 208, startup banner 209 |
| `dialogs.rc` | 25 recovered DIALOGEX templates + project dialogs 193–199/205/206 | 103–206 |
| `menu.rc` | Original RT_MENU 109 (10 top-level menus, 226 items) | 0x8000+ command IDs, `0x68` about, `0x69` exit |
| `accel.rc` | Original accelerators | 109, 176, 177, 178 |
| `assets.rc` | Embedded VCOD/TRIG, toolbar icon strip, menu banner icon | 148, 183, 193, 207, 208 |
| `control_ids.h` | `IDC_D###_####` macros for recovered dialogs | mirrors dialog control IDs |
| `app.ico` | Application icon; also resource `"130"` for dialog 103 | 130 |

`resources.rc` includes `dialogs.rc`, `assets.rc`, `menu.rc`, `accel.rc`, the icon, and
VERSIONINFO.

## Asset files

| File | Size / format | Purpose |
| --- | --- | --- |
| `assets/bin_148.bin` | 1 040 B | Default VCOD |
| `assets/bin_170.bin` | 18 953 B | Unit/sprite name tree (unused at runtime) |
| `assets/bin_183.bin` | 7 200 B | Default TRIG (3 triggers) |
| `assets/bin_193.bin` | 9 600 B | Default TRIG (4 triggers) |
| `assets/bitmap_134.bmp` | 16×16 4bpp | Original small bitmap |
| `assets/bitmap_151.bmp` | 500×300 | Original splash/help image |
| `assets/bitmap_152.bmp` | 128×128 | Startup banner (`IDB_START_BANNER` 209) shown when no map is open; content replaced with custom artwork, no longer the original bitmap |
| `assets/toolbar_icons.bmp` | 176×16, 24bpp, magenta transparent | 11 toolbar icons extracted from a screenshot |
| `assets/menu_icon.bmp` | 16×16 | Menu banner icon |

## Adding a resource

1. Put the file under `src/res/assets/`.
2. Add an unused ID in `src/res/resource.h` (dialogs from 195 upward, bitmaps from 207 upward).
3. Add the line to `src/res/assets.rc`.
4. Load it (`LoadImageW`, `FindResourceW`, or RCDATA helper); never rely on a path in the
   source tree at runtime.
5. Add a headless assertion (in `src/tests/headless_tests.cpp`) or extend
   `--dialog-audit-test` / `--menu-cmd-test` when the change is UI-visible.
6. Run the four-config build and both audit scripts.

## Lookup table

| Question | Read first |
| --- | --- |
| Where is a format documented? | `doc/01-archive/mpq-format.md`, `doc/02-chk/`, `doc/03-terrain/`, `doc/04-units/` |
| Which dialog ID maps to which feature? | `doc/05-ui/ui-map.md` |
| What did the original exe contain? | `doc/00-reverse/resource-inventory.md`, `reverse/scmdraft2/resources.csv` |
| How are menus/command IDs organized? | `src/ui/command_ids.h`, `doc/05-ui/ui-map.md` |
| How is a map loaded/saved? | `src/map/map_document.*`, `src/chk/chk_document.*` |
| How are tilesets decoded? | `src/terrain/tileset.*`, `src/terrain/terrain_renderer.*` |

## Build and verification

```powershell
# Four configurations
powershell -NoProfile -ExecutionPolicy Bypass -File tools\regression.ps1

# UI snapshot audit only
powershell -NoProfile -ExecutionPolicy Bypass -File tools\dialog-ui-check.ps1

# Headless tests only (example Debug/Win32)
bin\Win32\Debug\SCMDTests.exe --open="tools\repro\crash_repro.scm" --undo-test
```

`tools/regression.ps1` builds `SCMDPlus.exe` and `SCMDTests.exe`, audits dialogs on all
four configurations, then runs headless tests through `SCMDTests.exe` and UI probes
through `SCMDPlus.exe`. Baseline: 25 map tests, 21 pass; player/decor/sprite/radar fail
on `tools/repro/crash_repro.scm` because the sample lacks complete sections.
