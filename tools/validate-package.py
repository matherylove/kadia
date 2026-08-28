from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
errors = []

required = [
    'Kadia.pro', 'resources.qrc', 'kadia.rc',
    'src/main.cpp', 'src/kadia_window.cpp', 'src/kadia_scene.cpp',
    'src/d3d9_renderer.cpp', 'src/input_manager.cpp',
    'src/ffmpeg_runtime.cpp', 'src/ui_model.cpp',
    'src/game_stats.cpp', 'src/emulator_manager.cpp', 'src/kadia_settings.cpp',
    'assets/kadia_logo.argb',
    '.github/workflows/build-xp.yml',
    'tools/sync-console-logos.py',
    'third_party/ffmpeg/include/libavutil/avutil.h',
    'third_party/ffmpeg/lib/avutil.lib',
    'third_party/ffmpeg/avutil-59.dll',
]
for rel in required:
    if not (root / rel).exists():
        errors.append(f'missing: {rel}')

pro = (root / 'Kadia.pro').read_text(encoding='utf-8')
for rel in re.findall(r'\b(?:src|assets)/[A-Za-z0-9_./-]+\.(?:cpp|h|png|argb)', pro):
    if not (root / rel).exists():
        errors.append(f'Kadia.pro references missing file: {rel}')

try:
    tree = ET.parse(str(root / 'resources.qrc'))
    for node in tree.findall('.//file'):
        if not (root / (node.text or '')).exists():
            errors.append(f'resources.qrc references missing file: {node.text}')
except Exception as exc:
    errors.append(f'resources.qrc parse failed: {exc}')

cpp = (root / 'src/ui_model.cpp').read_text(encoding='utf-8')
if cpp.count('QStringLiteral(') < 800:
    errors.append('ui_model.cpp appears unexpectedly small')

workflow = (root / '.github/workflows/build-xp.yml').read_text(encoding='utf-8')
for needle in ['Qt-5.6.3-Static-XP.7z', 'Kadia.pro', 'Kadia.exe', 'Microsoft.VisualStudio.Component.WinXP',
               'sync-console-logos.py']:
    if needle not in workflow:
        errors.append(f'workflow missing expected token: {needle}')

renderer = (root / 'src/d3d9_renderer.cpp').read_text(encoding='utf-8')
for needle in ['Direct3DCreate9', 'CreateOffscreenPlainSurface', 'StretchRect', 'Present', 'D3DPRESENT_INTERVAL_ONE']:
    if needle not in renderer:
        errors.append(f'D3D9 renderer missing expected call: {needle}')


window = (root / 'src/kadia_window.cpp').read_text(encoding='utf-8')
for needle in ['showOnPrimaryMonitor', 'mouseMoveEvent', 'mousePressEvent', 'wheelEvent', 'setInterval(0)']:
    if needle not in window:
        errors.append(f'window/input implementation missing expected token: {needle}')

scene = (root / 'src/kadia_scene.cpp').read_text(encoding='utf-8')
for needle in ['setViewportSize', 'drawTileIcon', 'hoverAt', 'maxTrail']:
    if needle not in scene:
        errors.append(f'scene implementation missing expected token: {needle}')

for needle in ['ToggleGallery', 'CycleSort', 'LaunchSelectedGame', 'drawGallery']:
    if needle not in scene and needle not in window:
        errors.append(f'gallery implementation missing expected token: {needle}')


emulator = (root / 'src/emulator_manager.cpp').read_text(encoding='utf-8')
for needle in ['FullscreenHotkey', 'window:fullscreen=yes', '-video.fs', '-VICIIfull',
               'project64.exe', 'nestopia.exe']:
    if needle not in emulator:
        errors.append(f'emulator fullscreen implementation missing expected token: {needle}')

desktop = (root / 'src/desktop_capture.cpp').read_text(encoding='utf-8')
for needle in ['ensurePrintSurface', 'releasePrintSurface', 'g_lastCaptureUsedPrint',
               'prunePrintCaptureStates']:
    if needle not in desktop:
        errors.append(f'desktop capture optimization missing expected token: {needle}')

logo_sync = (root / 'tools/sync-console-logos.py').read_text(encoding='utf-8')
for needle in ['PRO100BYTE/console-logos', '1de47931607ddf83cbc982d776b68e6cc3864ad7',
               'Image.Resampling.LANCZOS', 'GITHUB_TOKEN', 'premultiplied-ARGB']:
    if needle not in logo_sync:
        errors.append(f'console logo sync missing expected token: {needle}')

for rel in [
    'assets/console_icons/playstation.argb',
    'assets/console_icons/playstation2.argb',
    'assets/console_icons/playstation3.argb',
    'assets/console_icons/psp.argb',
    'assets/console_icons/psvita.argb',
    'assets/console_icons/sega.argb',
    'assets/console_icons/atari.argb',
]:
    if not (root / rel).exists():
        errors.append(f'missing console icon asset: {rel}')

if errors:
    print('PACKAGE VALIDATION FAILED')
    for e in errors:
        print(' -', e)
    sys.exit(1)

print('PACKAGE VALIDATION OK')
print(' root:', root)
