# StarCraft Object Rendering (GRP / DAT)

Verified 2026-11-26 against the original editor's 4096×4096 map export (`origin.bmp`,
32 px/tile). Use this page for unit/sprite visuals; map/archive rules live in `mpq.md`.

## Data chain (VERIFIED)

```
units.dat    228 B            : u8  flingy index per unit type
flingy.dat   209 × u16        : sprite index
sprites.dat  517 × u16        : image index
images.dat   999 × u32        : images.tbl index (1-based)
images.tbl   u16 count + u16 offsets : C strings like neutral\min01.grp
```

- These files are **column-major** (struct-of-arrays): the first 999×4 bytes of
  `images.dat` are the GRP indices even though the file is 37 962 B total. Do not
  switch to a 38-byte record stride — it yields garbage.
- THG2 `type` is a **sprite id** (skip units/flingy). DD2 `type` is **not** a sprite id.
- Load order must prefer `BrooDat.mpq` over `StarDat.mpq` for BW data.
- Game MPQ folders: `arr\*.dat`, `unit\neutral\*.grp`, `unit\thingy\*.grp`,
  `unit\terran\*.grp`, `unit\zerg\*.grp`, `unit\protoss\*.grp`.
  Shadow GRPs live under the same folders, e.g. `unit\neutral\min01Sha.grp`.

### Key unit types

| Type | Meaning | GRP | Frame |
| --- | --- | --- | --- |
| 176/177/178 | Mineral Field (3 kinds) | `neutral\min01/02/03.grp` | 0 |
| 188 | Geyser | `neutral\geyser.grp` | **3** |
| 214 | Start Location | `thingy\StartLoc.grp` | 0 |
| 95 | Acritter | `neutral\Acritter.grp` | 0 (multi-frame animation) |

## GRP format (VERIFIED — direction was the mirroring bug)

Header `u16 frameCount, u16 width, u16 height`; then an 8-byte frame table per frame:
`u8 x, u8 y, u8 w, u8 h, u32 dataOffset`. Frame data starts with a `u16` per-row
offset table relative to `dataOffset`, followed by per-row RLE.

- **Rows are written left→right starting at `frame.x` with `x++`.**
  Decoding from `frame.x + w - 1` with `x--` mirrors every sprite horizontally.
  This was the root cause of “units facing backwards” (fixed in commit `4d129ce`).
- RLE: `0x00` no-op; `b & 0x80` skip `b & 0x7F` pixels; `b & 0x40` repeat next color
  `b & 0x3F` times; otherwise `b` literal palette indices.
- Decode into the full canvas (w×h); untouched pixels stay transparent. Palette is
  256 entries BGRA (tileset `.wpe` or a dedicated unit palette).
- Repeated frames share the same offset; the frame end is the next **greater** offset.

Verification metric: grayscale normalized cross-correlation (NCC) between the decoded
canvas and `origin.bmp` at the exact anchor, searching ±1 px. After the fix:
minerals 0.80–0.997, StartLoc 0.999, geyser frame 3 **1.000**. The wrong direction
scores < 0.2. Binary mask IoU is misleading (shadows and overlaps bias it).

## Drawing rules

- **Anchor**: GRP canvas center equals the CHK pixel coordinate:
  `left = x - canvasW/2`, `top = y - canvasH/2`. Verified on THG2 rocks, geyser and
  start location.
- **Shadows**: the shadow image is 1–2 `images.dat` entries after the object and its
  GRP name contains `Sha`/`Shad` (min01→min01Sha, geyser→geyShad, Acritter→nacShad).
  Draw it at the same anchor before the body. The shadow GRP uses only palette index 1,
  but the original does **not** paint that index's color: it composites the silhouette
  as **50% black over the terrain** (measured on `origin.bmp`: shadow ≈ terrain × 0.5,
  mean error 5.0; painting WPE index 1 opaque gives error 35.8). Implement by forcing
  shadow pixels to RGB(0,0,0) with alpha 128 inside the DIB.
  Start Location 214 is an editor marker — never draw a shadow for it.
  Shadow canvases are often larger than the body canvas (e.g. 128×96 vs 64×96), so
  centering the shadow canvas produces the intended down-left offset.
- **Team colors** use palette indices **8..15** and **0x88..0x8F** simultaneously.
  Measured from `origin.bmp` start locations: 9/10 = bright, 11 = mid, 12..15 = dark.
  Bright/mid/dark RGB (0..3 verified, 4..11 approximated in code):
  red (168,8,8)/(132,4,4)/(96,0,0); blue (8,52,152)/(4,32,100)/(0,8,80);
  teal (32,144,112)/(16,84,60)/(16,84,60); purple (136,64,156)/(104,48,120)/(72,28,80).
  Purple's bright shade is exactly `#88409C`. An ascending “index gets brighter”
  ramp is wrong and makes start markers too dark.
- **Neutral / resources** (owner ≥ 8, e.g. minerals, geyser, critters) must use the raw
  WPE palette — do not apply a team tint.
- **Minimap**: draw start locations as 3×3 white dots so they stand out.

## Palettes (partially VERIFIED)

Terrain uses the map tileset WPE (`ts/*.wpe`). Objects rendered by the original editor
do **not** match any of the eight WPE palettes: e.g. the mineral blues
`(36,60,100)`, `(52,76,108)`, `(56,92,148)` are absent from all WPE files, and a
dedicated unit palette must exist. Status: **TODO** — candidates `unit.pal`,
`unit\neutral\*.pal` etc. were not found in the StarCraft 1.08/1.16 MPQs; it may be
embedded in the exporter. Until located, object hues will differ slightly.

Useful indices used by sprites: minerals 193..199 and 43..50; geyser 16..245;
StartLoc only 9..15.

## Animation overlays (TODO)

The original preview animates geyser gas: `unit\thingy\GeySmok1..5.grp`
(32×64 canvas, 8 frames) plus `GeySmoS1`. Not drawn yet; see `doc/TODO.md`.

## UI coordinate system (VERIFIED)

- `MapViewRect()` is not at (0,0) (left sidebar 140 px, toolbar 28 px).
- Terrain is blitted at `(view.left, view.top)` from a full-map back buffer with source
  `(scrollX_, scrollY_)`.
- All overlays must share that origin: `SetViewportOrgEx(dc, view.left, view.top)`,
  clip to `{0,0,viewW,viewH}`, and pass the local rect to draw helpers.
- Mouse → map: `tx = (mx - view.left + scrollX_) / pxPerTile_` (same for pixels).
- `InvalidateRect` for tile-sized updates must add `view.left/view.top`.

## Verification recipes

```powershell
# Terrain baseline at 32 px/tile (compare to origin.bmp; mean diff ≈ 0.3)
bin\x64\Release\scmd_cli.exe render <map.scm> "$env:TEMP\terrain32.bmp" --scale 32 --ts exe\ts

# Extract a GRP entry (use & with quoted args; see mpq.md note)
& exe\SCMDPlus.exe --mpq=mpq\BrooDat.mpq --mpq-extract="unit\neutral\geyser.grp" `
  --extract-to="$env:TEMP\geyser.grp"
```

- Measure orientation with grayscale NCC, not mask IoU or screenshots.
- For screenshot probes: open with `--open=`, send `WM_LBUTTONDOWN/UP` with **client**
  coordinates (the DPI-unaware app does not scale injected mouse messages), and make
  the window topmost with `SetWindowPos(hwnd, HWND_TOPMOST, …, SWP_NOACTIVATE)`
  (never include `SWP_NOZORDER`, or topmost is ignored).
