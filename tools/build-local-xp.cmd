@echo off
setlocal

if "%QT_ROOT%"=="" set QT_ROOT=C:\Qt\5.6.3-Static-XP
if "%VS2017_PATH%"=="" set VS2017_PATH=C:\BuildTools2017

call "%VS2017_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 exit /b %errorlevel%

cd /d "%~dp0\.."
if exist build rmdir /s /q build
mkdir build
cd build

"%QT_ROOT%\bin\qmake.exe" -spec "%QT_ROOT%\mkspecs\win32-msvc2017" ..\Kadia.pro CONFIG+=release
if errorlevel 1 exit /b %errorlevel%

nmake
if errorlevel 1 exit /b %errorlevel%

echo.
echo Build complete. Locate Kadia.exe under the build directory.
endlocal
