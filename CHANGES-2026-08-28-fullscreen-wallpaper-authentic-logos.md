# 2026-08-28 fullscreen, live wallpaper and authentic platform logos

This patch focuses on three regressions reported while using Kadia as a full-screen
frontend on Windows.

## Emulator fullscreen

Kadia no longer treats fullscreen as one generic window hack.  Known standalone
emulators now use their own documented command-line option or native fullscreen
shortcut.  Project64 is launched normally and, after the actual game window
exists, Kadia uses Project64's Alt+Enter shortcut.  Nestopia UE receives its
native fullscreen option and classic Nestopia keeps its Win32 preference path,
with native Alt+Enter as a delayed fallback.  Additional mappings cover Mesen,
ares, Stella, Mednafen, Kega Fusion, Altirra, VICE, DOSBox, openMSX and Flycast.

The fallback never removes window styles or fabricates a borderless window.  If a
known emulator still has not switched itself after the native attempts, Kadia may
maximise the ordinary window as a final non-destructive fallback.

## Wallpaper Engine / live desktop

The compatibility PrintWindow capture path no longer allocates and destroys a
full-size DIB section every captured frame.  Kadia keeps a reusable scratch
surface for each wallpaper renderer window and periodically discards stale
entries.  Direct GetDC/StretchBlt capture remains the preferred path.

When PrintWindow is necessary, wallpaper sampling is capped at 12 Hz (and can
adapt lower when a capture is expensive).  Kadia itself continues rendering at
the monitor refresh rate and reuses the most recent wallpaper frame between
samples.  This keeps foreign paint requests from progressively starving
Wallpaper Engine.

## Authentic platform artwork

The standard GitHub Actions build now runs `tools/sync-console-logos.py` before
qmake.  It fetches the recommended Light/Color system marks from
`PRO100BYTE/console-logos` at a pinned repository snapshot, crops transparent padding, preserves aspect ratio and
encodes the images into Kadia's embedded premultiplied-ARGB resource format.
Runtime remains completely offline and does not require QtSvg or a PNG plugin.
Committed ARGB files remain as build-safe fallbacks if the external pack is not
available.
