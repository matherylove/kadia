# HTML → C++ mirror map

`design/kadia_html_reference.html` is the authoritative reference included in this package.

The C++ port intentionally keeps the mockup's fixed visual constants rather than replacing them with generic Qt layouts:

| HTML mockup | Native C++ |
|---|---|
| `.frame { inset:10px; border-radius:16px }` | dynamic client-sized frame with the same 10px inset + rounded clip |
| `.shell { padding:28px 44px 24px }` | frame inset + shell constants in `kadia_scene.cpp` |
| `.logo-stage { 64×48 }` | exact 64×48 target using supplied Kadia raster |
| `.hub { left:74px; top:6px; width:min(1180px,84vw) }` | the same live `min(1180px, 84% of client width)` calculation |
| category heights `34 / 52` | exact inactive/active metrics |
| tile `144×106` | exact inactive size |
| selected tile `198×146; margin-top:-12` | exact size/lift with 180ms interpolation |
| `.content-strip gap:14px` | exact 14px tile gap |
| description panel `width:300; padding:20px 22px` | exact 344px total panel width |
| game card `113×160` | exact inactive size |
| selected game `148×210` | exact selected size |
| game gap `15px` | exact 15px gap |
| Vista ribbon percentage geometry | transformed ellipse arcs using the same percentages/angles |
| Flying Stars JS equations | same perspective concept in C++, with close-star trail length clamped to prevent erroneous full-screen streaks |
| JS `model` array | generated directly into `src/ui_model.cpp` |

The one deliberate layout correction is the descriptive-panel collision seen in the browser mockup: the native tile/game viewport ends before the panel and is hard-clipped, so the same visual panel remains present without ever covering the selected tile.


## Native-resolution correction

The first C++ package treated 1280×720 as a virtual framebuffer and scaled the whole UI. That made CSS-pixel-sized UI elements too large on a 1080p monitor. The current port instead creates the QImage at the window's native client resolution and recalculates only the viewport-relative geometry. Fixed HTML pixel measurements remain fixed pixels, matching browser behavior much more closely.
