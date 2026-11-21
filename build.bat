@echo off
set "PATH=%PATH%;C:\Users\User\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"

echo ==============================================
echo [1/4] Copying WebView2Loader.dll for runtime...
copy /Y "webview2_sdk\build\native\x64\WebView2Loader.dll" "WebView2Loader.dll" >nul

echo [2/4] Compiling Windows Resources (Icon + Manifest)...
windres resource.rc -O coff -o resource.res

echo [3/4] Compiling Float Browser with C++ (g++ 16.2.0 - Static standalone)...
g++ -O3 -std=c++17 main.cpp resource.res -o Float.exe ^
    -I"webview2_sdk\build\native\include" ^
    -static -static-libgcc -static-libstdc++ ^
    -mwindows ^
    -lole32 -loleaut32 -luuid -ldwmapi -lcomctl32 -lshell32 -luser32 -lgdi32 -lshlwapi

if %ERRORLEVEL% EQU 0 (
    echo ==============================================
    echo [4/4] Build SUCCESS! Created Float.exe
    echo ==============================================
) else (
    echo ==============================================
    echo Build FAILED with error %ERRORLEVEL%
    echo ==============================================
)

