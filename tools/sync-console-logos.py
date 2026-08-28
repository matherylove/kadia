#!/usr/bin/env python3
"""Replace Kadia's fallback platform artwork with authentic console logo-pack art.

Source: PRO100BYTE/console-logos, "Recommended Versions (Normal) (1 Per Platform) v2.1"
The repository is MIT licensed and mirrors Dan Patrick's professionally redrawn +
official system logo set.  At build time we fetch the small Light - Color PNG for
each platform, crop transparent padding, resize it, and encode Kadia's tiny raw
premultiplied-ARGB resource format.  Runtime stays fully offline and QtSvg-free.
"""

from __future__ import annotations

import argparse
import io
import json
import os
import re
import struct
import sys
import urllib.parse
import urllib.request
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:  # pragma: no cover - exercised in CI after pip install
    raise SystemExit("Pillow is required: python -m pip install pillow") from exc

REPO = "PRO100BYTE/console-logos"
# Pin the pack snapshot so CI produces the same embedded artwork even if the
# upstream default branch changes later.
REF = "1de47931607ddf83cbc982d776b68e6cc3864ad7"
BASE = "Recommended Versions (Normal) (1 Per Platform) v2.1/Light - Color"
API = f"https://api.github.com/repos/{REPO}/contents"

# output basename -> (pack category, preferred source-name aliases)
TARGETS = {
    # Consoles
    "nes": ("Consoles", ["Nintendo Entertainment System", "Nintendo NES", "NES"]),
    "snes": ("Consoles", ["Super Nintendo Entertainment System", "Super Nintendo", "SNES"]),
    "n64": ("Consoles", ["Nintendo 64", "N64"]),
    "gamecube": ("Consoles", ["Nintendo GameCube", "GameCube"]),
    "wii": ("Consoles", ["Nintendo Wii", "Wii"]),
    "wiiu": ("Consoles", ["Nintendo Wii U", "Wii U", "WiiU"]),
    "switch": ("Consoles", ["Nintendo Switch", "Switch"]),
    "mastersystem": ("Consoles", ["Sega Master System", "Master System"]),
    "genesis": ("Consoles", ["Sega Genesis", "Sega Mega Drive", "Mega Drive", "Genesis"]),
    "saturn": ("Consoles", ["Sega Saturn", "Saturn"]),
    "dreamcast": ("Consoles", ["Sega Dreamcast", "Dreamcast"]),
    "playstation": ("Consoles", ["Sony PlayStation", "PlayStation", "Playstation 1"]),
    "playstation2": ("Consoles", ["Sony PlayStation 2", "PlayStation 2", "PS2"]),
    "playstation3": ("Consoles", ["Sony PlayStation 3", "PlayStation 3", "PS3"]),
    "xbox": ("Consoles", ["Microsoft Xbox", "Xbox"]),
    "xbox360": ("Consoles", ["Microsoft Xbox 360", "Xbox 360"]),
    "atari2600": ("Consoles", ["Atari 2600"]),
    "atari5200": ("Consoles", ["Atari 5200"]),
    "atari7800": ("Consoles", ["Atari 7800"]),
    "pcengine": ("Consoles", ["NEC PC Engine", "PC Engine", "TurboGrafx 16", "TurboGrafx-16"]),
    "neogeo": ("Consoles", ["SNK Neo Geo AES", "Neo Geo AES", "SNK Neo Geo", "Neo Geo"]),

    # Handhelds
    "gameboy": ("Handhelds", ["Nintendo Game Boy", "Game Boy"]),
    "gameboycolor": ("Handhelds", ["Nintendo Game Boy Color", "Game Boy Color"]),
    "gba": ("Handhelds", ["Nintendo Game Boy Advance", "Game Boy Advance"]),
    "nds": ("Handhelds", ["Nintendo DS", "DS"]),
    "n3ds": ("Handhelds", ["Nintendo 3DS", "3DS"]),
    "psp": ("Handhelds", ["Sony PlayStation Portable", "PlayStation Portable", "Sony PSP", "PSP"]),
    "psvita": ("Handhelds", ["Sony PlayStation Vita", "PlayStation Vita", "PS Vita"]),
    "gamegear": ("Handhelds", ["Sega Game Gear", "Game Gear"]),
    "atarilynx": ("Handhelds", ["Atari Lynx", "Lynx"]),
    "ngp": ("Handhelds", ["SNK Neo Geo Pocket", "Neo Geo Pocket"]),
    "ngpc": ("Handhelds", ["SNK Neo Geo Pocket Color", "Neo Geo Pocket Color"]),
    "wonderswan": ("Handhelds", ["Bandai WonderSwan", "WonderSwan"]),
    "wonderswancolor": ("Handhelds", ["Bandai WonderSwan Color", "WonderSwan Color"]),

    # Computers
    "msx": ("Computers", ["MSX", "Microsoft MSX"]),
    "c64": ("Computers", ["Commodore 64", "C64"]),
    "amiga": ("Computers", ["Commodore Amiga", "Amiga"]),
    "dospc": ("Computers", ["Microsoft DOS", "MS-DOS", "DOS", "IBM PC", "PC"]),

    # Arcade frontends / generic arcade
    "mame": ("Arcade", ["MAME"]),
    "fbneo": ("Arcade", ["Final Burn Neo", "FinalBurn Neo", "FBNeo"]),
    "arcade": ("Arcade", ["Arcade Classics", "Arcade"]),
}


def _norm(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", text.lower())


def _request(url: str) -> bytes:
    headers = {"User-Agent": "Mathery-Kadia-logo-sync/1.0", "Accept": "application/vnd.github+json"}
    token = os.environ.get("GITHUB_TOKEN", "").strip()
    if token:
        headers["Authorization"] = f"Bearer {token}"
        headers["X-GitHub-Api-Version"] = "2022-11-28"
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=45) as response:
        return response.read()


def _directory(category: str) -> list[dict]:
    path = f"{BASE}/{category}"
    url = f"{API}/{urllib.parse.quote(path, safe='/')}?ref={REF}"
    data = json.loads(_request(url).decode("utf-8"))
    if not isinstance(data, list):
        raise RuntimeError(f"Unexpected GitHub response for {category}: {data!r}")
    return [entry for entry in data
            if entry.get("type") == "file"
            and str(entry.get("name", "")).lower().endswith(".png")
            and not str(entry.get("name", "")).startswith("._")]


def _score(name: str, aliases: list[str]) -> tuple[int, int]:
    stem = Path(name).stem
    n = _norm(stem)
    best = -1
    for index, alias in enumerate(aliases):
        a = _norm(alias)
        if not a:
            continue
        preference = max(0, 80 - index * 8)
        if n == a:
            score = 10000 + preference
        elif n.startswith(a):
            score = 9000 + preference - max(0, len(n) - len(a))
        elif a.startswith(n) and len(n) >= 4:
            score = 8200 + preference - max(0, len(a) - len(n))
        elif a in n:
            score = 7600 + preference - max(0, len(n) - len(a))
        else:
            # Token overlap is only a last resort and is deliberately weak.
            at = set(re.findall(r"[a-z0-9]+", alias.lower()))
            nt = set(re.findall(r"[a-z0-9]+", stem.lower()))
            overlap = len(at & nt)
            score = overlap * 500 + preference if overlap else -1
        best = max(best, score)
    # Prefer shorter names when scores tie: they are usually the canonical mark,
    # not a regional/anniversary/alternate variant.
    return best, -len(name)


def _pick(entries: list[dict], aliases: list[str]) -> dict | None:
    ranked = sorted(entries, key=lambda e: _score(str(e.get("name", "")), aliases), reverse=True)
    if not ranked:
        return None
    best = ranked[0]
    if _score(str(best.get("name", "")), aliases)[0] < 500:
        return None
    return best


def _encode_argb(png_bytes: bytes, max_extent: int = 320) -> bytes:
    image = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    alpha = image.getchannel("A")
    bbox = alpha.getbbox()
    if bbox:
        image = image.crop(bbox)
    if image.width <= 0 or image.height <= 0:
        raise ValueError("empty transparent image")
    image.thumbnail((max_extent, max_extent), Image.Resampling.LANCZOS)

    out = bytearray(struct.pack("<II", image.width, image.height))
    for r, g, b, a in image.getdata():
        # QImage::Format_ARGB32_Premultiplied is BGRA in little-endian memory.
        pr = (r * a + 127) // 255
        pg = (g * a + 127) // 255
        pb = (b * a + 127) // 255
        out.extend((pb, pg, pr, a))
    return bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="assets/console_icons", help="destination folder")
    parser.add_argument("--strict", action="store_true", help="fail if any mapped logo is unavailable")
    parser.add_argument("--min-success", type=int, default=34,
                        help="minimum successful replacements before returning failure")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    output = (root / args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)

    categories: dict[str, list[dict]] = {}
    for category in sorted({v[0] for v in TARGETS.values()}):
        print(f"Fetching logo index: {category}")
        categories[category] = _directory(category)

    success = 0
    missing: list[str] = []
    for basename, (category, aliases) in TARGETS.items():
        source = _pick(categories[category], aliases)
        if not source or not source.get("download_url"):
            missing.append(basename)
            print(f"WARNING: no authentic pack match for {basename}: {aliases}")
            continue
        try:
            png = _request(str(source["download_url"]))
            encoded = _encode_argb(png)
            target = output / f"{basename}.argb"
            target.write_bytes(encoded)
            success += 1
            print(f"  {basename:18s} <- {source['name']}")
        except Exception as exc:
            missing.append(basename)
            print(f"WARNING: failed {basename} from {source.get('name')}: {exc}")

    print(f"Authentic console logos updated: {success}/{len(TARGETS)}")
    if missing:
        print("Fallback artwork retained for: " + ", ".join(missing))
    if args.strict and missing:
        return 2
    if success < args.min_success:
        print(f"ERROR: only {success} logos updated; minimum is {args.min_success}", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
