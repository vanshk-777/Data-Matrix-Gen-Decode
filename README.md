# dmgen — Data Matrix Barcode Generator

A fast, dependency-light C++ CLI tool for generating **Data Matrix** (ISO/IEC 16022) barcodes as **SVG** or **PNG** files. Designed for rapid label generation in manufacturing, logistics, and asset-tracking workflows.

---

## Architecture

### Library: libdmtx

The encoder is built on [**libdmtx**](https://github.com/dmtx/libdmtx) (v0.7.8), the canonical open-source C implementation of the Data Matrix standard. It handles:

- Reed-Solomon ECC200 error correction
- Symbol layout (finder pattern, clock tracks, data region)
- All 30 standardised symbol sizes (square and rectangular)
- Multiple encoding schemes (ASCII, C40, Text, X12, EDIFACT, Base256)

We use it purely for encoding — libdmtx writes pixel data into an internal `DmtxImage` buffer which we then read back to produce our own SVG/PNG output, giving us full control over output format and quality.

### PNG output: stb_image_write

Rather than pulling in libpng (a heavyweight dependency with a complex API), PNG output uses [**stb_image_write**](https://github.com/nothings/stb) — a single-header public-domain library. It is vendored directly into `src/stb_image_write.h` so the project has zero runtime install requirements beyond libdmtx.

### JSON parser: hand-rolled, zero deps

The JSON batch parser (`parseJSON()` in `dmgen.cpp`) is a small bespoke implementation (~80 lines). It handles:
- Arrays of plain strings: `["ITEM-001", "ITEM-002"]`
- Arrays of objects with `"data"` and optional `"filename"` keys
- Mixed arrays (strings and objects together)
- JSON string escaping (`\"`, `\\`, `\n`, `\t`, etc.)

No `nlohmann/json` or similar was used — the schema is simple enough that a minimal parser keeps the build truly dependency-free.

---

## How Encoding Works

```
Input string
     │
     ▼
dmtxEncodeCreate()          ← allocate encode context
dmtxEncodeSetProp(...)      ← module size, margin, symbol size
dmtxEncodeDataMatrix(n, s)  ← encode payload → DmtxImage pixel buffer
     │
     ▼
Read pixel buffer (bottom-up, grayscale)
Threshold each module centre pixel (< 128 → dark)
     │
     ▼
Grid{ rows, cols, cells[] }  ← module-level boolean grid
     │
     ├──▶ writeSVG()   → <rect> per dark module, pure SVG
     └──▶ writePNG()   → upscaled grayscale raster via stb_image_write
```

**Why read pixel-by-pixel instead of querying the matrix directly?**
libdmtx's public API does not expose the raw codeword/module matrix — it only provides the rendered pixel image. We sample the centre of each module cell (accounting for the bottom-up row order libdmtx uses internally) to reconstruct the boolean module grid cleanly.

**Why SVG as the default output?**
SVG is vector — it scales to any printer DPI without aliasing. A 10×10 module SVG printed at 600 DPI looks identical to one printed at 2400 DPI. For PNG, the module size (`-m`) directly sets the pixel count per module, so you must size it appropriately for your target DPI.

---

## Project Layout

```
data-matrices-qr/
├── src/
│   ├── dmgen.cpp           # Main generator (~480 lines, C++17)
│   └── stb_image_write.h   # Vendored header-only PNG writer
├── examples/
│   ├── labels.json         # JSON batch input example
│   └── labels.csv          # CSV batch input example
├── out/                    # Default batch output directory
├── Makefile
└── README.md
```

---

## Build

### Requirements

- macOS (Apple Silicon or Intel)
- Xcode Command Line Tools: `xcode-select --install`
- libdmtx: `brew install libdmtx`

### Compile

```bash
make          # → ./dmgen
make test     # → smoke tests into test_out/
make clean    # → remove binary and test_out/
```

The Makefile links directly against `/opt/homebrew/lib/libdmtx` without requiring `pkg-config`:

```makefile
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -I/opt/homebrew/include -Isrc
LDFLAGS  = -L/opt/homebrew/lib -ldmtx
```

---

## Usage

### Single label

```bash
# SVG (default, recommended for printing)
./dmgen -d "pallet 234" -o out/pallet_234.svg

# PNG, 15px per module
./dmgen -d "pallet 234" -o out/pallet_234.png -m 15

# Force a fixed 24×24 symbol size
./dmgen -d "pallet 234" -o out/pallet_234.svg -s 24x24
```

### Batch — JSON

```bash
./dmgen --json examples/labels.json --out-dir out/
./dmgen --json examples/labels.json --out-dir out/ --fmt png -m 12
```

**JSON format** — array of objects or plain strings (or mixed):

```json
[
  { "data": "pallet 234", "filename": "pallet_234" },
  { "data": "SM3",        "filename": "sm3" },
  { "data": "rack 17",   "filename": "rack_17" }
]
```

- `"data"` — the payload to encode (required)
- `"filename"` — output filename stem, no extension (optional; derived from data if omitted)

### Batch — CSV

```bash
./dmgen --csv examples/labels.csv --out-dir out/ --fmt svg
```

**CSV format** — `data,filename` (filename column optional, `#` lines are comments):

```
# Data: payload,filename
pallet 234,pallet_234
SM3,sm3
rack 17,rack_17
```

### All options

| Flag | Default | Description |
|---|---|---|
| `-d <data>` | — | Payload to encode (single mode) |
| `-o <file>` | `<data>.svg` | Output path (`.svg` or `.png`) |
| `--json <file>` | — | JSON batch input |
| `--csv  <file>` | — | CSV batch input |
| `--out-dir <dir>` | `out` | Output directory for batch mode |
| `--fmt svg\|png` | `svg` | Batch output format |
| `-m <px>` | `10` | Module (cell) size in pixels (1–100) |
| `-s <NxN>` | `auto` | Symbol size — see `--sizes` |
| `--sizes` | — | List all valid symbol sizes and exit |
| `-h`, `--help` | — | Show help |

### Symbol sizes (`-s`)

`auto` selects the smallest square symbol that fits the payload (recommended). Fixed sizes:

```
Square:      10x10  12x12  14x14  16x16  18x18  20x20  22x22  24x24  26x26
             32x32  36x36  40x40  44x44  48x48  52x52  64x64  72x72  80x80
             88x88  96x96  104x104  120x120  132x132  144x144

Rectangular: 8x18  8x32  12x26  12x36  16x36  16x48
```

---

## Output sizing guide

| Use case | Recommended flags |
|---|---|
| Laser marking (small part) | `-m 1 -s 10x10` then scale SVG in your engraver software |
| Thermal label printer (203 DPI) | `-m 4` (PNG) or SVG |
| Thermal label printer (300 DPI) | `-m 6` (PNG) or SVG |
| Desktop inkjet / laser | `-m 10` SVG (let printer handle DPI) |
| Large warehouse shelf label | `-m 20` or larger, SVG |

---

## Example outputs

The three default labels (`pallet 234`, `SM3`, `rack 17`) auto-select these symbol sizes:

| Label | Symbol | Why |
|---|---|---|
| `SM3` | 10×10 | 3-char payload → smallest possible symbol |
| `rack 17` | 14×14 | 7 chars including space → needs slightly more data capacity |
| `pallet 234` | 16×16 | 10 chars → next fitting square |
