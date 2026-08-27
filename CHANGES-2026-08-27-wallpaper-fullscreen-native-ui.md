# Mathery Kadia - wallpaper layer, fullscreen enforcement and native UI hotfix

## Wallpaper-only live background
- Removed composited-screen capture from DesktopWallpaper mode.
- Kadia no longer uses WDA_EXCLUDEFROMCAPTURE to create fake transparency.
- Wallpaper Engine renderer windows are captured directly when present.
- Generic animated wallpaper renderers parented to Explorer's wallpaper WorkerW are supported as a fallback.
- Explorer desktop icons, taskbars and ordinary application windows are never captured by this path.
- If no live wallpaper renderer can be identified, the existing static Windows wallpaper fallback remains in the scene.

## Native-resolution UI
- Removed whole-frame adaptive downscaling.
- Text, tiles, cover art, logos and one-pixel UI decoration now rasterize at native client resolution.
- D3D9 uses point/no filtering for 1:1 presentation to prevent softening.
- The expensive animated decorative backdrop is rendered separately and may be capped at 1920x1080 on larger monitors; the UI is composited over it at full native resolution.
- Live wallpaper capture is sampled at at most 60 Hz so high-refresh UI frames are not blocked by redundant GDI capture work.

## Fullscreen launch reliability
- Kept documented fullscreen command-line switches for supported emulators.
- Added a Win32 borderless-fullscreen fallback that follows the launched process and child processes for approximately 12 seconds.
- The fallback selects the largest visible emulator window, strips window chrome/menu and sizes it to the monitor bounds without stealing focus.
- Native/exclusive fullscreen windows are detected and left untouched.
- ShellExecute fallback now asks Windows to show the associated emulator maximized and obtains its process ID when available.
- Added/expanded emulator discovery for Vita3K and additional classic-system standalone emulators.
- Fixed the previous PlayStation Vita fallthrough that could incorrectly auto-map Vita titles to DuckStation.
- PCSX2/other ROM paths remain canonical absolute paths before the emulator working directory changes.
- Returning focus to Kadia no longer automatically marks a game session finished while the launched emulator process tree is still alive.
