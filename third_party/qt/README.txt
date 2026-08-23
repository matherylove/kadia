Qt itself is not duplicated into this repository.

The supplied Sightline project builds against this XP-compatible static archive:
https://github.com/lighterowl/qt563xp/releases/download/1.2/Qt-5.6.3-Static-XP.7z

.github/workflows/build-xp.yml downloads that exact archive automatically and
extracts it to C:\Qt\5.6.3-Static-XP before invoking qmake.
