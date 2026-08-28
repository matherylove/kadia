# Third-party console marks

Kadia's platform tiles use **real system-logo artwork from an external logo pack**, not Qt-drawn approximations.

The standard GitHub Actions build runs `tools/sync-console-logos.py` before qmake. The script downloads the **Light - Color / Recommended Versions (Normal)** entries from `PRO100BYTE/console-logos` (snapshot `1de47931607ddf83cbc982d776b68e6cc3864ad7`), crops transparent padding, preserves each logo's aspect ratio, and converts the images to Kadia's local premultiplied-ARGB resource format. The compiled program therefore has no SVG plugin or network dependency at runtime.

`PRO100BYTE/console-logos` (snapshot `1de47931607ddf83cbc982d776b68e6cc3864ad7`) describes the collection as professionally redrawn plus official videogame/computer system logos, mirrored from Dan Patrick's logo set. The repository code/artwork collection is distributed with an MIT license; trademarks and console logos remain property of their respective owners.

Repository: https://github.com/PRO100BYTE/console-logos
License: MIT

The ARGB files committed in `assets/console_icons/` remain offline/bootstrap fallbacks so the source tree can still be opened and built without network access. The supported CI build refreshes those fallbacks with authentic pack artwork before compilation. The older PlayStation/Sega/Atari seed assets were derived from Simple Icons (CC0-1.0).
