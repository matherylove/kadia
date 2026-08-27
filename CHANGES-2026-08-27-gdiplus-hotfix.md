# GDI+ startup crash hotfix

Fixed a Windows startup crash in `BackgroundSettings::loadWithGdiPlus()`.

The previous implementation called `GdiplusShutdown()` while a stack-allocated
`Gdiplus::Bitmap` was still alive. Its destructor then called
`GdipDisposeImage()` after GDI+ had already shut down, which could produce an
`INVALID_POINTER_READ` in `GdiPlus.dll` during startup when restoring a desktop
or custom wallpaper.

The bitmap and all associated locked image state are now destroyed before
`GdiplusShutdown()` is called.
