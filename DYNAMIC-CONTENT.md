# Dynamic content implementation notes

## Render order

1. Kadia dark base gradient
2. user image / Windows wallpaper at configured opacity
3. Vista/Aero ribbons and haze
4. Win98 starfield
5. vignette
6. top bar, menus, tiles, descriptions and footer

This is why changing the background never removes the animated visual identity.

## ROM classification persistence

The scan only enumerates filenames/metadata; it does not open or modify ROM contents. The full path is SHA-1 keyed in `rom-catalog.ini` so paths containing slashes remain safe as INI group names. `None (ignore)` is a permanent classification and prevents repeat prompts. `Unknown` keeps the file visible in Kadia's dynamic Unknowns category.

## Store detection

Storefront visibility is rebuilt from `StoreDetector::detectInstalledStores()` before `KadiaScene` is created. If a store is missing, its tile is removed rather than disabled.

## WinDS PRO

The MediaFire landing page is used as the stable configured source. The worker parses the current expiring `download*.mediafire.com` URL each time instead of embedding an ephemeral direct URL.
