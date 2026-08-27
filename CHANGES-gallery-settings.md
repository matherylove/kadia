# Kadia gallery/settings completion

This revision completes the previously planned gallery and direct-control work:

- Animated carousel-to-gallery transition.
- Gallery toggle: Tab, middle mouse button, or XInput Back/Select.
- Sort cycle: Y on XInput or S on keyboard.
- Sort modes: alphabetical, release date, played/unplayed, play time, date added.
- Persistent per-ROM launch count, last-played timestamp, date-added timestamp and accumulated play time.
- Double-click/A/Enter launch through EmulatorManager, with WinDS PRO auto-detection and Windows file-association fallback.
- Kadia Settings button and dialog for default gallery sorting, clock visibility and custom background.
- Direct actions for Windows power, controller, sound and core system-settings tiles; Windows Media Center-specific media functions remain delegated to their media backend.
- Local console-brand assets for available Simple Icons marks, with no runtime SVG dependency.
- ScreenScraper release year is retained alongside Libretro release metadata.

Compatibility target remains Qt 5.6.3, C++11, x86 and Windows subsystem 5.1.
