# GUI performance hotfix

- Removed the per-frame `detectedSystemKey()` construction that normalized every ROM path, sorted the complete library and joined it into a string on every `kadiaSections()` query.
- Added an O(1) section-model revision counter. Console/catalog visibility is rebuilt only when detected games, unknown ROMs or store detection actually changes.
- Empty-console detection now scans the game library only once per section-model revision and builds a compact `QSet` of populated platform labels; individual tiles no longer rescan the complete game list.
- Media library scans now run at `QThread::LowestPriority` so filesystem indexing yields more aggressively to Kadia's render/input threads.
- Large media-list rebuilds suppress per-row repaint and selection notifications until the batch is complete.
