# Mathery Kadia - performance/stability pass (2026-08-27)

This pass addresses the issues reproduced after the first gallery/settings integration.

- Fixed mojibake/UI separator glitches by removing broken encoded literals and repairing legacy display strings when loading metadata.
- Reworked library/carousel layout so the game strip reserves the details-panel area and no longer slides underneath titles/details at common desktop widths.
- Cover art is decoded as bounded thumbnails on the global worker pool; entering a library no longer synchronously decodes visible full-size images on the GUI thread.
- Removed the unconditional full-drive ROM scan shortly after startup when a recognized catalog already exists. A full refresh remains available through Update Gamelist / Scrape Now; an empty first-run catalog still triggers discovery.
- Removed game-stats.ini from the startup/write hot path. The legacy file is neither parsed nor rewritten. Runtime statistics now live in compact game-stats-v2.ini and are written only for games that are actually launched/played.
- Avoided full game-library rebuilds when a single launched game returns to Kadia; only that record is refreshed.
- Kadia stops rendering and releases its D3D9 device during external emulator/fullscreen handoff, then recreates the device when Kadia becomes active again. This avoids the previous background device-loss/crash path.
- Added concrete per-console categories for every system exposed by RomCatalog (home consoles, handhelds, computers and arcade) instead of manufacturer-only grouping.
- Added per-platform local badges/icons for concrete system categories, while retaining bundled PlayStation/Sega/Atari marks where available.
- Kept the Windows XP / Qt 5.6.3 / C++11 build constraints and package validator intact.

Local package validation: `PACKAGE VALIDATION OK`.
The definitive MSVC/Qt 5.6.3 Windows XP compilation still runs in the repository's CI/build environment.
