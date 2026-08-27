# Kadia refresh / marquee / live desktop / emulator fullscreen pass

- Single-line labels that overflow their available region now use a phone-style marquee on the active item: pause, smooth scroll to the far edge, pause, and return. The animation is time-based rather than frame-count based.
- D3D9 now selects the adapter attached to Kadia's monitor and reports that adapter's real refresh rate. Presentation remains `D3DPRESENT_INTERVAL_ONE`, so the zero-interval render pump is VSync-limited to the monitor refresh rather than a fixed 60 Hz timer.
- Kadia keeps UI layout and mouse hit-testing at native client coordinates while using an adaptive internal QPainter raster surface on high-resolution/high-refresh displays. D3D9 performs the final scale to the actual back buffer.
- Background mode 1 is now a live desktop passthrough. On Windows 10 2004+ Kadia excludes its own top-level window from capture and captures the composited desktop directly, preserving Wallpaper Engine and other animated desktop content. Older Windows versions fall back to the wallpaper host/static wallpaper without creating a recursive capture.
- ROM launch paths are canonicalized to absolute native paths before the emulator working directory changes. This fixes PCSX2 reporting that relative ISO paths do not exist.
- PCSX2 Qt uses `-fullscreen -batch -- <path>`; old wx PCSX2 keeps its compatible `--fullscreen --nogui` form.
- Fullscreen launch arguments were added for detected DuckStation, PPSSPP, melonDS, mGBA, Snes9x, Dolphin, Cemu, RPCS3, Yuzu, Ryujinx, Xenia, MAME, Project64 and Redream executables where their frontends expose stable CLI controls.
