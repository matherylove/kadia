# Empty catalogs + Windows Media Center phase 1

- Console, handheld, classic-computer and arcade catalog tiles are now data-driven. A platform tile is present only when at least one recognized game can actually enter that catalog; the whole section disappears if no platform remains.
- Navigation indices are clamped immediately when a section disappears after rescan/reclassification.
- Added an on-demand media-library backend for Music, Videos/Movies, Pictures and Recorded TV. Media folders are never scanned at Kadia startup.
- Added a threaded media browser with search and Windows file-association playback/opening.
- Added Play All through a generated UTF-8 M3U playlist after the background scan has finished.
- Added Tasks -> Media Libraries for configuring the folders indexed for each media type.
- Wired the first WMC tiles: Home/Music, Music Library/Search/Play All, TV + Movies/Recorded TV/Movies/Search, Pictures + Videos/Picture Library/Slide Show/Video Library/Search/Play All, and Tasks/Media Only/Media Libraries.
- Live TV, Guide/tuner setup, Radio, Now Playing, optical-disc playback, online providers and the in-process decoder/player remain for later media-backend phases.
