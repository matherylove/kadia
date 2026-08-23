# Mathery Kadia! — Qt 5.6 / Direct3D 9 / Windows XP

Native C++ port of the supplied Kadia HTML mockup. The project deliberately keeps the visual structure and data model of `design/kadia_html_reference.html` instead of redesigning it into a different Qt UI.

## Rendering path

- **100% C++** — no QML, no Qt WebEngine, no embedded browser.
- **Qt 5.6.3** for the window, fonts, input events, image resources and QPainter rasterization.
- **Direct3D 9** owns presentation to the native Qt `HWND`.
- The UI is rasterized to the same fixed 1280×720 virtual surface every frame and copied into a D3D9 `X8R8G8B8` offscreen surface. `StretchRect` scales that exact surface to the physical back buffer with aspect-ratio-preserving letterboxing, following the proven XP-era path from Sightline. This keeps positions, proportions and animation timing deterministic across machines.
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
- Keyboard and XInput navigation.
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
| F11 | Toggle fullscreen/windowed |

Kadia starts fullscreen. Use `Kadia.exe --windowed` for development.

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
- `src/d3d9_renderer.cpp` — native D3D9 device, BGRA offscreen surface, `StretchRect` scaling and presentation; adapted from the supplied Sightline D3D9 presenter approach.
- `src/kadia_window.cpp` — fullscreen native Qt window and keyboard/controller dispatch.
- `src/input_manager.cpp` — dynamically loaded XInput 1.3 / 9.1.0 fallback for XP.
- `src/ffmpeg_runtime.cpp` — supplied FFmpeg dependency integration.
- `design/kadia_html_reference.html` — the exact HTML used as the porting reference.

## XP notes

The source intentionally avoids DWM, Direct2D, DirectWrite, WASAPI, WebEngine, QML and post-XP Win32 APIs. D3D9 and dynamically loaded XInput are used directly. If XInput is not installed on an XP machine, keyboard input still works and Kadia does not fail to start because no XInput import library is linked.
