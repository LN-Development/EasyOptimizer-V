# EasyOptimizer-V

A high-performance GTA V texture optimizer and manager written in native C/C++ for Windows.  
Edit YTD files without extracting, find duplicates, resize, generate mips, and more.

---

## Features

### File Support
| Format | Read | Write | Notes |
|--------|------|-------|-------|
| `.ytd` | ✅ | ✅ | GTA V texture dictionary (RSC7, Legacy & Gen9) |
| `.wtd` | ✅ | ✅ | GTA IV streaming texture dictionary (RSC5) |
| `.ydr` | ✅ | — | GTA V drawable (embedded textures extracted) |
| `.yft` | ✅ | — | GTA V fragment (embedded textures extracted) |
| `.ydd` | ✅ | — | GTA V drawable dictionary (embedded textures extracted) |
| `.rpf` | ✅ | — | RPF7 archives — AES, NG and CFXP (OpenIV/FiveM mods) decryption supported |

### Texture Operations
- **Smart Optimize** — Batch resize and recompress with configurable max resolution, format, and mipmap settings
- **Fast Recompress** — One-click recompression with optional format downgrade (e.g. BC3 → BC1 when alpha is unused) to maximise space savings
- **Recompress Same Format** — Re-encode without changing format, resolution, or mip count
- **Per-Texture Editing** — Right-click any texture card to resize, convert format, export as DDS, unload changes (revert to original), or remove
- **Mipmap Generation** — Auto-generate a full mip chain using stb_image_resize2 (Mitchell filter)
- **DDS Export** — Export individual textures as DDS files

### Duplicate Management
- **Detect Duplicates** — Find duplicate textures across all loaded files by name, data hash (SHA-256), or both simultaneously
- **Migrate Dups** — Preview the migration before applying: consolidated YTDs are shown in the UI with an amber border marked `PREVIEW`; choose "Maintain" per consolidated file to keep originals, then click Migrate to commit

### Encoding
- **CPU Encoder** — bc7enc_rdo with ISPC acceleration (BC1, BC2, BC3, BC4, BC5, BC7)
- **GPU Encoder** — NVIDIA Texture Tools 3 (NVTT 3.2.5+) for BC1/BC3/BC7 via CUDA
- Toggle between CPU and GPU with the sidebar button; the log always reports which encoder is active

### Loading & Organisation
- **Folder Scanner** — Recursively scan directories for all supported file types; bulk loads start collapsed for a clean overview
- **RPF Archives** — Load `.rpf` files directly; entries are listed under the RPF group and can be expanded individually
- **Drag & Drop** — Drop files or folders directly into the window
- **Import Filter** — Choose which file types to import (YTD / WTD / YFT / YDD / YDR / RPF) per session
- **Grid Sizes** — Cycle texture cards between Small (160×200), Medium (220×260), and Native (300×340)
- **Sorting** — Sort loaded archives by name, type, total size, texture count, or modified state
- **Unload / Remove** — Right-click any archive or texture to remove it from the workspace (disk untouched) or revert edits back to the on-disk original

### UI & Languages
- Dark theme with colour-coded size indicators (green ≤ 12 MiB → red > 64 MiB for whole archives)
- Supports **15 languages**: English, Portuguese, Spanish, Russian, Turkish, Mandarin Chinese, Hindi, Japanese, Arabic, Bengali, French, German, Indonesian, Korean, Italian

---

## Saving

`Save All` creates a versioned project cache under  
`projects\project-YYYYMMDD-HHMMSS-mmm\` next to the executable before writing any files, then offers:

- **Replace originals** — overwrite the source YTD/WTD files
- **Save to folder** — copy to a chosen directory
- **Cache only** — keep a snapshot without touching originals

YDR/YFT/YDD model inputs are **read-only**: their extracted textures can be saved as standalone YTD copies, but the original model files are never overwritten.  
RPF entries loaded from archives are also read-only — the source RPF is never modified.

---

## Building

### Requirements

- **Windows 10 or later**
- **Visual Studio 2019+** with the C++ desktop workload (`cl.exe`, `link.exe`)
- No additional downloads needed — all libraries are vendored

### Steps

1. Open a **Developer Command Prompt for Visual Studio** (or run `vcvars64.bat` manually)
2. Navigate to this directory
3. Run:

```bat
build.bat
```

The compiled executable is placed at `build\EasyOptimizer-V.exe`.

### GPU Encoding (optional)

Place `nvtt30205.dll` (NVIDIA Texture Tools 3.2.5+) and `vcomp140.dll` beside the executable to enable GPU-accelerated BC1/BC3/BC7 compression via CUDA.  
The app probes for the DLL at startup and reports its status in the log. Falls back to CPU encoding automatically if unavailable.

### NG-encrypted RPF Support (optional)

Reading **NG-encrypted** RPF archives (vanilla GTA V game files) needs `ng.dat` and `lut.dat`. You can generate them yourself from your own game copy — **no key material is distributed**:

1. Click **Get GTA Keys** in the sidebar (or accept the prompt that appears when you open an NG-encrypted RPF without keys).
2. Select your `GTA5.exe`.
3. The app scans the executable for the key windows (matched against the SHA-1 fingerprints in the bundled `gtav_key_hashes.dat`) and writes `ng.dat` + `lut.dat` next to itself.

This mirrors CodeWalker's approach: only SHA-1 hashes are shipped, never the keys themselves — the keys are derived from your own installation. **AES-encrypted** and **CFXP** (OpenIV/FiveM mod) RPFs work without any key files.

---

## Project Structure

```
src/
  gui.c               — Main window, sidebar, all dialogs and UI logic
  gui_cards.c         — Texture card and archive card rendering
  ytd.c               — GTA V YTD (RSC7) loader/saver
  wtd.c               — GTA IV WTD (RSC5) loader/saver
  ydr.c               — YDR/YFT/YDD embedded texture extractor
  rpf_scan.cpp        — RPF7 archive scanner (AES / NG / CFXP decryption)
  optimizer.c         — Smart resize, duplicate finder, migration engine
  texture.c           — Mipmap generation, format conversion, snapshot/revert
  bc7enc_wrapper.cpp  — bc7enc_rdo ISPC integration
  nvtt_c_wrapper.c    — NVTT 3 GPU encoder wrapper
  theme.c             — Dark theme colours and fonts
  hash.c              — Jenkins one-at-a-time hash
  dds.c               — DDS file export
  main.c              — Entry point, crash handler
vendor/               — Third-party libraries (bc7enc_rdo, stb, miniz)
res/                  — Windows resources (manifest, version info)
build.bat             — Build script
```

---

## Credits & Licenses

This project was built with knowledge and inspiration from the following works:

| Project | Author | License |
|---------|--------|---------|
| [FiveFury](https://github.com/Hancapo/fivefury) | Hancapo | MIT |
| [CodeWalker](https://github.com/dexyfex/CodeWalker) | dexyfex | Educational |
| [bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo) | Rich Geldreich | MIT |
| [stb](https://github.com/nothings/stb) | Sean Barrett | Public Domain |
| [miniz](https://github.com/richgel999/miniz) | Rich Geldreich | MIT |

Special thanks to **ook3d** for WTD support.

---

## License

This program is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License v3** as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful, but **WITHOUT ANY WARRANTY**; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the [GNU General Public License](./LICENSE) for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
