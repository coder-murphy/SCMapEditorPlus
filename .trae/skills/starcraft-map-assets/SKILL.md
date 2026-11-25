---
name: starcraft-map-assets
description: Unpack ScmDraft2 and StarCraft MPQ assets, extract tilesets, and organize editor resources. Use for dumping original resources, MPQ/CHK inspection, or asset pipeline work. Not for ordinary map edits.
---

# StarCraft Map Assets

Apply this skill whenever a task touches the original ScmDraft2 resources, StarCraft MPQ
archives, tilesets, CHK data, or asset IDs. The repository already contains verified
parsers, dumps, and docs; reuse them instead of re-deriving formats.

## Order of reading

1. `references/resource-map.md` — where every resource and tool lives.
2. `references/original-editor.md` — dumping and interpreting ScmDraft2 resources.
3. `references/mpq.md` — reading/writing MPQ, compression, and tileset export.
4. `references/objects.md` — GRP decoding, unit/sprite drawing rules, team colors,
   shadows, palette findings, and the viewport coordinate system.

## Non-negotiable rules

- Never modify `reverse/scmdraft2/raw/`, `mpq/*.mpq`, or user maps in place. Copy to
  `%TEMP%` or a work directory first.
- Map MPQ slot lookup uses **hash type 0**; locale must be `0x0000`; `(listfile)` must exist.
- Encrypted entries derive the key with `hash(basename, 3)`, plus `blockPos`, then XOR
  `fileSize` when flag `0x00020000` (FIX_KEY) is present.
- zlib sectors carry a 2-byte `78 9C` header; PKWARE implode expands to a 64-entry
  distance table. Do not "simplify" either case.
- `RT_BITMAP` payload is a DIB. Prepend the 14-byte BMP file header when exporting.
- Preserve original command IDs and resource IDs. New IDs start at 195 for dialogs and
  207 for bitmaps; never reuse an existing ID.
- Source files are UTF-8 with BOM; `.rc` files with Chinese text must set
  `#pragma code_page(65001)` or stay ASCII-only.
- Headless verification goes through `SCMDTests.exe`; `SCMDPlus.exe` keeps only UI probes.
- Mark findings `VERIFIED` or `TODO` in `doc/`; never present a guess as a fact.
- **GRP rows decode left→right from `frame.x` (`x++`)**. Decoding right→left mirrors every
  unit/sprite — this was the “units face backwards” bug. Validate with grayscale NCC
  against `origin.bmp` (≥0.99), never with mask IoU alone.
- Object palette: team color is indices 8..15 **and** 0x88..0x8F (9/10 bright, 11 mid,
  12..15 dark); neutral/resources (owner ≥ 8) stay untinted. Shadows are the `*Sha*`
  image 1–2 `images.dat` entries after the body and use the same center anchor.

## Quick commands

```powershell
# Dump every PE resource of the original editor into reverse/scmdraft2/
powershell -NoProfile -ExecutionPolicy Bypass -File tools\dump_original.ps1

# Build one configuration
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" `
  SCMDPlus.sln /t:Build /p:Configuration=Debug /p:Platform=Win32 /m /v:m

# Headless map/MPQ inspection (after the build)
bin\Win32\Debug\SCMDTests.exe --mpq="<map.scx>"
bin\Win32\Debug\SCMDTests.exe --open="<map.scx>" --section-test

# Extract one MPQ entry (tool mode in the GUI binary).
# In PowerShell call it with & and quoted args; a Start-Process -ArgumentList array
# mangles backslash paths and ends with "extract=FAIL args". --extract-to is a FILE path.
& bin\Win32\Debug\SCMDPlus.exe --mpq="<archive.mpq>" --mpq-extract="arr\units.dat" `
  --extract-to="$env:TEMP\units.dat"
& bin\Win32\Debug\SCMDPlus.exe --mpq="mpq\BrooDat.mpq" `
  --mpq-extract="unit\neutral\geyser.grp" --extract-to="$env:TEMP\geyser.grp"

# Terrain-only baseline for object alignment checks (4096×4096 at 32 px/tile)
bin\Win32\Debug\scmd_cli.exe render "<map.scm>" "$env:TEMP\terrain32.bmp" --scale 32 --ts exe\ts

# Export the eight tilesets from StarDat/BrooDat
bin\Win32\Debug\scmd_cli.exe extract-ts "F:\Games\单机游戏\starcraft" "%TEMP%\ts"
```

## Typical workflows

- **Add or fix a UI feature** — find the original dialog/menu ID in
  `doc/00-reverse/resource-inventory.md`, read the matching block in
  `src/res/dialogs.rc` or `src/res/menu.rc`, implement, then run
  `tools\dialog-ui-check.ps1` and `tools\regression.ps1`.
- **Add a resource** — put the file under `src/res/assets/`, add an ID in
  `src/res/resource.h`, add the line to `src/res/assets.rc`, load it with `LoadImageW`,
  and extend the audit or a headless test.
- **Inspect a map or archive** — start with `SCMDTests.exe --mpq=<path>`; it reports
  entry count, CHK size, and DIM to `%TEMP%\scmdplus_mpq.txt`.
- **Extract or switch tilesets** — export with `scmd_cli extract-ts`, keep `ts/` in the
  repository, and pass `--ts=<dir>` to tests. The build copies `ts/` next to both EXEs.
- **Fix unit/sprite visuals (orientation, shadow, color)** — read `references/objects.md`.
  Render terrain only with `scmd_cli render --scale 32`, decode the GRP at the CHK anchor,
  and score with grayscale NCC against `origin.bmp` (target ≥0.99). Use the
  `.wpe` palette for now; the dedicated object palette is still a TODO.

## Continuous reinforcement loop

After every reverse-engineering finding:

1. Write the fact into the relevant `doc/00-reverse/` or `doc/01-archive/` page with
   `VERIFIED` or `TODO` and the evidence (offset, sample, command).
2. If behavior changes, add or extend a headless test in `src/tests/headless_tests.cpp`
   or a UI probe in `src/app/app.cpp`.
3. Update the affected reference file in this skill when a path, ID, or rule changed.
4. Rebuild four configurations, run `tools\dialog-ui-check.ps1`, then
   `tools\regression.ps1 -Map tools\repro\crash_repro.scm`.
5. Commit code, docs, and skill changes together so the next session inherits them.

Known baseline failures with `tools/repro/crash_repro.scm`: player/decor/sprite/radar
fail because that sample lacks complete sections. Do not treat them as regressions.

## Boundaries

- Do not guess binary layouts from other games; this project targets StarCraft 1 (SCM/SCX)
  and ScmDraft2 0.8.0.0.
- Do not add new third-party libraries for MPQ/CHK parsing; extend `src/archive`,
  `src/chk`, and `src/map` instead.
- Do not edit generated `.rc` files by hand without updating the corresponding reverse doc.
