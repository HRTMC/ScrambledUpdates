@echo off
setlocal

if not defined VCPKG_ROOT (
    echo ERROR: VCPKG_ROOT is not set.
    echo        Clone vcpkg, run bootstrap-vcpkg.bat, and set VCPKG_ROOT to it.
    exit /b 1
)

cmake --preset release || exit /b 1
cmake --build --preset release || exit /b 1

copy /y "%~dp0build\release\Release\ScrambledUpdates.dll" "%~dp0build\ScrambledUpdates.dll" >nul || exit /b 1
copy /y "%~dp0build\release\Release\ScrambledUpdates.pdb" "%~dp0build\ScrambledUpdates.pdb" >nul || exit /b 1

echo.
echo Built: %~dp0build\ScrambledUpdates.dll
endlocal
