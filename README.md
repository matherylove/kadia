# Mathery Kadia! — Qt 5.6 / Direct3D 9 / Windows XP

Native C++ port of the supplied Kadia HTML mockup. The project deliberately keeps the visual structure and data model of `design/kadia_html_reference.html` instead of redesigning it into a different Qt UI.

## Current revision fixes

- Star streaks are length-clamped, so close stars can no longer produce erroneous lines crossing most of the screen.
- Animation delivery is VSync-capped to the D3D9 adapter/primary-monitor refresh rate, with high-resolution delta timing.
- The scene is rasterized at the current client resolution instead of a fixed 720p intermediate.
- Default launch is a normal frameless window covering the primary monitor, not exclusive/fullscreen and not always-on-top.
- Mouse hover/click/right-click/wheel input is implemented natively.
- Tile glyphs are native vector geometry and the model source is ASCII-safe, eliminating XP/MSVC codepage mojibake.

## Rendering path

- **100% C++** — no QML, no Qt WebEngine, no embedded browser.
- **Qt 5.6.3** for the window, fonts, input events, image resources and QPainter rasterization.
- **Direct3D 9** owns presentation to the native Qt `HWND`.
- The UI now renders directly at the **actual client resolution of the primary monitor**. The original HTML's CSS-pixel measurements remain fixed (44px active category, 144×106 tiles, etc.) instead of scaling a 720p canvas, so 1080p/1440p/4K screens no longer make every UI element oversized. The BGRA frame is copied into a same-size D3D9 `X8R8G8B8` surface for presentation.
- Windows XP target is **x86 / subsystem 5.1**.

The Direct3D device is not ANGLE or an OpenGL compatibility layer: `src/d3d9_renderer.cpp` calls `Direct3DCreate9` and presents the finished frame directly.

## What is mirrored from the HTML

- Kadia dark/cosmic-latte palette.
- Animated Vista-inspired ribbons and glow.
- Dense Windows 98 Flying Stars-style starfield.
- Exact Kadia raster logo asset, idle breathing, shine sweep and physical slice glitch.
- WMC-style vertical category rail.
- Horizontal tile rows with selected-tile expansion and scrolling.
- The full **27-section / 211-tile** RetroBat + Windows Media Center model from the supplied HTML.
- Persistent description panel in its own reserved right-hand column, so it cannot cover tiles.
- Game-library page with its own reserved description column.
- WMC-style bottom-right transport controls.
- Keyboard, XInput **and native mouse** navigation. Mouse hover selects items; left click activates tiles; right click goes back; mouse wheel browses rows/sections.
- D3D9 VSync presentation is capped by the primary monitor's actual refresh rate instead of a hard-coded 60 FPS timer.
- Tile icons are rendered as native QPainter vector geometry instead of depending on Unicode symbol-font/codepage behavior on XP.
- F cycles the same Aero/Corbel/XP-safe wordmark font concept used in the mockup.

## Controls

| Input | Action |
|---|---|
| Arrow Up / Down | Change section |
| Arrow Left / Right | Browse tiles / games |
| Enter | Select / open game library |
| Escape / Backspace | Back |
| Xbox A | Select |
| Xbox B | Back |
| D-pad | Navigate |
| F | Cycle wordmark font fallback |
| Mouse hover | Select category / tile / game |
| Left click | Select / activate tile |
| Right click | Back |
| Mouse wheel | Browse row or sections |
| F11 | Toggle borderless primary-monitor mode / windowed mode |

Kadia starts as a **normal borderless window sized to the complete primary monitor**, not as an exclusive/fullscreen Qt window and not always-on-top. Alt+Tab, the Windows key and normal task switching remain available. Use `Kadia.exe --windowed` for a centered 1280×720 development window.

## Dependencies reused from Sightline

The uploaded Sightline project was used as the dependency/toolchain source as requested:

- `third_party/ffmpeg/` is copied from Sightline, including headers, `.lib` files, XP runtime DLLs and the archived large `avcodec-61.dll`.
- The GitHub Actions build uses the same **Qt 5.6.3 Static XP** archive URL.
- The workflow uses the same **Visual Studio 2017 + v141 XP component** approach and verifies an x86 PE subsystem of **5.1**.
- The original Sightline workflow is retained as a non-executing reference at `reference/sightline-build-xp.yml`; Kadia's adapted build is `.github/workflows/build-xp.yml`.

FFmpeg is already linked into Kadia and `FfmpegRuntime` probes its version at startup. The visual mockup itself does not decode media yet; the dependency is present so the frontend can add media playback without changing the XP build layout.

## Build locally

Use the same Qt package as the workflow and Visual Studio 2017 Build Tools with the Windows XP component installed.

```bat
call C:\BuildTools2017\VC\Auxiliary\Build\vcvarsall.bat x86
mkdir build
cd build
C:\Qt\5.6.3-Static-XP\bin\qmake.exe -spec C:\Qt\5.6.3-Static-XP\mkspecs\win32-msvc2017 ..\Kadia.pro CONFIG+=release
nmake
```

Before running the executable, extract `third_party\ffmpeg\avcodec-61.7z` and place all five FFmpeg runtime DLLs next to `Kadia.exe`:

- `avcodec-61.dll`
- `avformat-61.dll`
- `avutil-59.dll`
- `swscale-8.dll`
- `swresample-5.dll`

The GitHub Actions workflow does this automatically.

## Source map

- `src/kadia_scene.cpp` — literal visual scene and animations.
- `src/ui_model.cpp` — generated directly from the HTML model: all 27 sections and 211 options.
- `src/d3d9_renderer.cpp` — native D3D9 device, BGRA offscreen surface, VSync-to-monitor-refresh presentation; adapted from the supplied Sightline D3D9 presenter approach.
- `src/kadia_window.cpp` — borderless primary-monitor Qt window plus keyboard/controller/mouse dispatch.
- `src/input_manager.cpp` — dynamically loaded XInput 1.3 / 9.1.0 fallback for XP.
- `src/ffmpeg_runtime.cpp` — supplied FFmpeg dependency integration.
- `design/kadia_html_reference.html` — the exact HTML used as the porting reference.

## XP notes

The source intentionally avoids DWM, Direct2D, DirectWrite, WASAPI, WebEngine, QML and post-XP Win32 APIs. D3D9 and dynamically loaded XInput are used directly. If XInput is not installed on an XP machine, keyboard input still works and Kadia does not fail to start because no XInput import library is linked.


## Icon refresh

- Tile icons are now drawn with a semantic vector icon system instead of generic glyph fallbacks.
- Each option gets a unique icon signature derived from its section + label, so repeated labels across different sections no longer reuse the same exact icon.
- Icons remain native `QPainter` geometry for Qt 5.6 / XP compatibility.
