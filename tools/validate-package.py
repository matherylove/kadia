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
    'assets/kadia_logo.argb', 'design/kadia_html_reference.html',
    '.github/workflows/build-xp.yml',
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
for needle in ['Qt-5.6.3-Static-XP.7z', 'Kadia.pro', 'Kadia.exe', 'Microsoft.VisualStudio.Component.WinXP']:
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

if errors:
    print('PACKAGE VALIDATION FAILED')
    for e in errors:
        print(' -', e)
    sys.exit(1)

print('PACKAGE VALIDATION OK')
print(' root:', root)
