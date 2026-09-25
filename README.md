# TermRender

A terminal path tracer that renders **glTF / GLB** scenes using lower-half-block
glyphs (`▄`). Every character cell carries **two** pixels: the glyph's foreground
colour draws one scanline and its background colour draws the other, so a normal
terminal cell grid becomes a full-resolution, per-pixel ray tracer.

It is a plain C++17 program (no graphics API, no window) that only needs
terminal input/output and file reading. On **Windows** use **MinGW-w64** and run
`build.ps1`; on **Linux** and **macOS** use **gcc** and run `build.sh`.

## Renderer

The rendering pipeline is a faithful CPU translation of Dassault Systèmes'
[`dspbr-pt`](https://github.com/DassaultSystemes-Technology/dspbr-pt):

- the `material.glsl` closure builder,
- the slang-pbr material kernel (`webgl-full` profile; `pbr_kernel.cpp`),
- the `misptdl` MIS path-tracing integrator,
- `lighting.glsl`, `utils.glsl`, `constants.glsl`, `rng.glsl`,
- the `tonemap.frag` operators and the perspective-camera ray convention.

Everything runs on the CPU (no GPU). Material layers are evaluated exactly as in
the reference: multi-scatter energy compensation, thin-film iridescence,
anisotropic GGX, thin/thick dielectric transmission with dispersion, sheen and
clearcoat. The same constants are used (`EPS_COS` / `EPS_PDF = 1e-3`,
`MINIMUM_ROUGHNESS = 1e-4`, `TFAR_MAX = 1e5`, fixed Russian-roulette of 0.1 from
depth 2, up to 32 specular bounces, `ray_eps = 1e-4`), the same Marsaglia-MWC RNG
(with a fresh, uncorrelated seed per accumulation batch), and the same
half-vector / pdf handling (a sample whose pdf falls below `EPS_PDF` terminates
the path, matching the reference).

What is **ours** (not part of dspbr-pt): the TUI, the camera controls, the
asynchronous scheduling, the procedural fallback environment, NEE against
emissive geometry (dspbr-pt has none), NEE against the procedural environment,
and the multi-light punctual sampling (dspbr-pt has a single point light).

## Features

- **TUI** laid out like the `sshfm` TUI: a reverse-video title bar, a content
  area, a reverse-video key-hint band, a message row and a single-line prompt.
- **Grapheme-cluster aware input** (ported from `sshfm`): cursor movement,
  backspace/delete and clipping operate on whole clusters (combining marks, emoji
  ZWJ sequences, flags, skin-tone modifiers, ...); the prompt scrolls by display
  width. At startup the terminal is **calibrated** for how it really renders emoji
  using cursor-position reports.
- **Asynchronous progressive rendering**:
  - A batch is traced on a worker thread while the main thread keeps servicing
    input, so **keys are always responsive**.
  - The partial image is refreshed on screen **at least once per second**.
  - **Every completed batch is painted immediately** (a finished frame is never
    dropped).
  - **Movement keys** are ignored while the current accumulation's *first* batch
    is still rendering (so the first image always completes); once it is on
    screen, the next movement key **immediately cancels the running batch and
    restarts** with the new camera. Rapid input coalesces instead of starving the
    display.
  - **Any other key** works at any time; if it changes the view/settings, the
    running batch is cancelled and restarted so the change always takes effect.
  - Loading a new model clears the previous image (no stale frame).
- **Half-block rendering**: foreground = even scanlines, background = odd
  scanlines, glyph = `▄`. **TrueColor (24-bit)** and **256-colour** modes.
- The terminal size is **queried ten times per second** through the ANSI
  `CSI 18 t` report; a resize re-allocates the framebuffer.
- **PBR metallic-roughness** (GGX / Smith / Schlick-Fresnel) with correct sRGB vs.
  linear handling.
- **Textures**: base colour, metallic-roughness, normal, occlusion and emissive
  maps, with wrap modes, **mipmapped trilinear filtering** and
  `KHR_texture_transform`; multiple `TEXCOORD` sets; `COLOR_0` vertex colours.
- **Material extensions**: `KHR_materials_ior`, `KHR_materials_specular`,
  `KHR_materials_sheen` (Charlie NDF + Neubelt), `KHR_materials_clearcoat`,
  `KHR_materials_transmission`, `KHR_materials_volume` (Beer-Lambert media),
  `KHR_materials_iridescence` (thin-film interference), `KHR_materials_anisotropy`,
  `KHR_materials_dispersion` (channel-roulette IOR), `KHR_materials_unlit`, and the
  legacy `KHR_materials_pbrSpecularGlossiness`. As in the reference revision,
  `KHR_materials_emissive_strength` is **not** applied (emission is
  `emissiveFactor × emissiveTexture`).
- **Alpha**: `MASK` cutouts and `BLEND` transparency (unbiased stochastic
  transparency in the integrator). Back faces are **not** culled (matching
  dspbr-pt); two-sidedness is handled by flipping normals toward the ray.
  Shadow rays pass through cutout/transparent surfaces.
- **Emissive geometry emits real light**: emissive triangles are importance
  sampled with **NEE + MIS** (against BSDF sampling), so emissive materials
  actually illuminate the scene; they are also visible directly.
- **Background / IBL**: a Radiance `.hdr` environment can be set in Settings
  (empty = procedural neutral gradient). The HDR is luminance-importance-sampled
  (marginal/conditional CDFs) and MIS-combined with BSDF sampling; the procedural
  gradient is cosine-sampled with NEE.
- **All punctual lights** from `KHR_lights_punctual`: directional, point and spot
  with range/cone attenuation and NEE (shadow rays).
- **Tone mapping**: `None` (linear, default), `Reinhard`, `Cineon`, `ACES`
  (full matrices) and `Uncharted 2`, plus gamma `pow(1/2.2)`; exposure in EV.
  Radiance is clamped to a configurable threshold (default **3.0**, `0` = off),
  matching the reference default.
- **Skinned meshes** are rendered in their bind pose (no animation is played).
- **Parallel rendering**: scanlines are claimed dynamically across all CPU cores
  (with a `sysconf` fallback), with prompt cancellation between rows.

## Requirements

- A C++17 compiler: MinGW-w64 `g++` on Windows; `gcc` on Linux and macOS.
- A UTF-8 terminal with virtual-terminal support (e.g. **Windows Terminal** on
  Windows).

No external libraries need to be installed: the two single-header dependencies
are bundled under `deps/` (`cgltf.h`, `stb_image.h`) and compiled into the binary.

## Building

**Windows** — MinGW-w64 `g++` in `PATH`, run from PowerShell:

```powershell
.\build.ps1            # release build -> out\termrender.exe
.\build.ps1 -Debug     # debug build
```

**Linux / macOS** — `g++` in `PATH`:

```bash
./build.sh             # release build -> out/termrender
./build.sh --debug     # debug build
```

## Usage

```bash
termrender
```

The program takes no arguments; everything is done from the TUI. Choose
**Start Render...**, type a `.gltf` or `.glb` path and press Enter.

### TUI

- **Up / Down** – move the selection, **Enter** – activate.
- **Start Render...** – asks for a `.gltf`/`.glb` path and enters the render view.
- **Settings** lets you change:
  - Colour mode: TrueColor / 256 colours
  - Samples per frame (default 48)
  - Max bounces (default 24)
  - Exposure (EV)
  - Environment brightness
  - Clamp radiance (default 3.00; set to `off` to disable)
  - Tonemap: None (linear) / Reinhard / Cineon / ACES / Uncharted 2
  - Background (.hdr): type a Radiance `.hdr` path (empty = procedural gradient)

### Render view

- **Arrow keys** – look around (yaw / pitch)
- **W / A / S / D** – move (S = backward)
- **E / Q** – move up / down
- **C** – movement-speed multiplier
- **Ctrl+C** – turn-speed multiplier
- **F** – field of view (degrees), or **+ / -** step by step
- **Ctrl+S** – settings page
- **Ctrl+Q** – return to the main TUI

Movement is horizontal: **W / S** follow the view's yaw but ignore its pitch; **A
/ D** strafe and **E / Q** move straight up / down.

As described under *Asynchronous progressive rendering*, holding still keeps
adding samples (see the status bar), while camera/settings changes cancel and
restart the accumulation.

## How the pixels map to cells

For terminal cell row `r`, framebuffer rows `2k` and `2k+1` are drawn with one
`▄`:

- `2k` (even) → **foreground** colour → the glyph's lower half
- `2k+1` (odd) → **background** colour → the glyph's upper half

so a `C`-column by `R`-row terminal yields a `C × 2R` pixel image. In 256-colour
mode each pixel is mapped to the nearest xterm-256 entry through a cached
`32×32×32` lookup table.

## Project layout

```
TermRender/
├─ build.ps1            Windows build (MinGW-w64)
├─ build.sh             Linux / macOS build (gcc)
├─ assets/
│  ├─ scene.glb             sample: textured ground, red cube, emissive lamp, lights
│  ├─ emissive_test.glb     diffuse ground lit only by an emissive panel
│  ├─ spheres.glb           checker ground + mirror sphere + emissive sphere
│  └─ venice_sunset_1k.hdr  sample HDR environment
├─ deps/
│  ├─ cgltf.h           glTF/GLB parser (single header)
│  └─ stb_image.h       PNG/JPEG/BMP/TGA/GIF decoder (single header)
├─ tools/               dev-only generators (not part of the build)
│  ├─ gen_gltf.cpp          -> assets/scene.glb
│  ├─ gen_emissive_glb.cpp  -> assets/emissive_test.glb
│  ├─ gen_spheres_glb.cpp   -> assets/spheres.glb
│  └─ convert_pbr.ps1       regenerates src/pbr_kernel.cpp from the slang-pbr kernel
├─ src/
│  ├─ main.cpp          entry point
│  ├─ tui.h/.cpp        TUI (menu, settings, prompts, async render loop)
│  ├─ term.h/.cpp       raw mode, size query, key input, emoji calibration
│  ├─ textutil.h/.cpp   UTF-8 / grapheme-cluster width & editing (from sshfm)
│  ├─ term_render.h/.cpp half-block / colour encoder
│  ├─ renderer.cpp      path tracer (dspbr-pt port) + BVH + integrator
│  ├─ renderer.h        Renderer API / RenderSettings
│  ├─ pbr_kernel.cpp    translated slang-pbr material kernel (generated)
│  ├─ pbr.h             stable kernel API
│  ├─ pbr_prelude.h     GLSL-like math shims used by the kernel
│  ├─ envmap.h/.cpp     HDR environment + importance sampling
│  ├─ gltf_loader.h/.cpp glTF/GLB -> Scene
│  ├─ image.h/.cpp      image decoding
│  ├─ scene.h           scene / material / light structures
│  └─ math3d.h          vectors, matrices, sampling, RNG
```

## Limitations

- Animated / skinned meshes are loaded in their bind pose (no playback).
- As in the reference revision, `KHR_materials_emissive_strength` is ignored.
- The procedural (non-HDR) environment uses cosine-sampled NEE at every
  non-specular hit, which costs an extra shadow ray per bounce; load an HDR for
  the importance-sampled path.
