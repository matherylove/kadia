# Windows XP build notes

The supported release configuration mirrors the Sightline toolchain supplied with this task:

1. Windows runner/build machine with Visual Studio 2017 Build Tools.
2. Install `Microsoft.VisualStudio.Component.WinXP`.
3. Use the Qt archive referenced by `.github/workflows/build-xp.yml`.
4. Build x86 with qmake + nmake.
5. Keep `/SUBSYSTEM:WINDOWS,5.01` and `_WIN32_WINNT=0x0501`.
6. Ship the supplied XP FFmpeg DLLs beside `Kadia.exe`.

The workflow verifies the output PE machine type (`0x014C`) and subsystem version (`5.1`) and reports common Vista-or-later API names as warnings for manual inspection.

## Direct3D 9

Kadia does not depend on Qt's OpenGL/ANGLE choice. The renderer uses `Direct3DCreate9` directly. This avoids ambiguity about whether a particular Qt build selected desktop OpenGL, ANGLE D3D9 or ANGLE D3D11.

The UI is rendered at the HTML port's fixed 1280×720 virtual resolution into BGRA and copied into a D3D9 `X8R8G8B8` offscreen surface. `StretchRect` performs the final aspect-ratio-preserving GPU scale to the back buffer, matching the XP-compatible approach already used in Sightline.
