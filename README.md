# EasyOptimizer-V

A high-performance GTA V texture optimizer written in C.

## Features

- **YTD/WTD/YDR/YFT/YDD support** — Load texture dictionaries and extract embedded textures from drawables, fragments, and drawable dictionaries
- **Smart Optimize** — Batch resize and recompress textures with configurable max resolution, format, and mipmap settings
- **Fast Recompress** — One-click recompression of all loaded textures using the active encoder
- **CPU & GPU Encoding** — Toggle between bc7enc (CPU) and NVTT (GPU via `nvtt.dll`) for BC1/BC3/BC5/BC7 compression
- **Per-Texture Editing** — Resize, change format, and set mipmaps for individual textures
- **Duplicate Finder** — Find duplicate textures by name or data hash across all loaded files
- **Folder Scanner** — Recursively scan directories for all supported file types
- **Drag & Drop** — Drop files directly into the window to load them

- **Sorting** - Sort loaded archives by name, type, total size, texture count, or modified state
- **Languages** - Switch the main interface between 15 languages: English, Portuguese, Spanish, Russian, Turkish, Mandarin Chinese, Hindi, Japanese, Arabic, Bengali, French, German, Indonesian, Korean, and Italian

## Saving

`Save All` creates a versioned project cache under
`projects\project-YYYYMMDD-HHMMSS-mmm\` next to the executable before writing
files. It then offers:

- **Yes** - replace original YTD/WTD files
- **No** - save copies to another folder
- **Cancel** - keep files unchanged

The same popup also offers a cache-only snapshot without replacing files.
YDR/YFT/YDD inputs are read-only models: their extracted textures can be saved
as YTD copies, but the original model files are never overwritten.

## Building

### Requirements

- **Windows 10+**
- **Visual Studio 2019+** (MSVC toolchain with `cl.exe`)
- No external dependencies needed — all libraries are vendored

### Steps

1. Open a **Developer Command Prompt for Visual Studio** (or run `vcvars64.bat`)
2. Navigate to this directory
3. Run:

```bat
build.bat
```

The compiled executable will be at `build\EasyOptimizer-V.exe`.

### Optional: GPU Encoding

Place `nvtt.dll` (from NVIDIA Texture Tools) next to the executable to enable GPU-accelerated texture compression.

## Project Structure

```
src/            — Source code
  gui.c         — Main window, sidebar, dialogs
  gui_cards.c   — Texture card rendering
  ytd.c         — GTA V YTD (RSC7) loader/saver
  wtd.c         — GTA IV WTD (RSC5) loader/saver
  ydr.c         — YDR/YFT/YDD embedded texture extractor
  optimizer.c   — Smart resize & duplicate finder
  texture.c     — Mipmap generation & format conversion
  bc7enc_wrapper.cpp — bc7enc_rdo integration
  nvtt_c_wrapper.c   — NVTT GPU encoder (optional)
  theme.c       — Dark theme colors & fonts
  hash.c        — Jenkins one-at-a-time hash
  dds.c         — DDS file export
  main.c        — Entry point
vendor/         — Third-party libraries (bc7enc_rdo, stb)
res/            — Windows resource files (manifest, icon)
build.bat       — Build script
```

## Credits, Acknowledgments & Licenses

This project was built with knowledge and inspiration and use code from the following projects:

- **[FiveFury](https://github.com/Hancapo/fivefury)** by Hancapo — MIT License
- **[CodeWalker](https://github.com/dexyfex/CodeWalker)** by dexyfex — Educational Purposes Only
- **bc7enc_rdo** — MIT License
- **stb** (stb_image, stb_image_resize2) — Public Domain
- **miniz** — MIT License
- **Thanks** to ook3d for .wtd support


## License

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of - Big objects auto divider and converter with non embedd collision
- Auto Ambient Occlusion
 be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the [GNU General Public License](./LICENSE) for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
