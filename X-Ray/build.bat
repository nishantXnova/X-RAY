@echo off
setlocal

set "PROJ=%~dp0"
set "SRC=%PROJ%src"
set "OUT=%PROJ%build\xray.exe"

echo Building X-RAY AI Hardware Diagnostic Agent...
echo.

g++ -std=c++17 -Wall -Wextra -O2 -I "%PROJ%include" ^
    "%SRC%\collector.cpp" ^
    "%SRC%\config_manager.cpp" ^
    "%SRC%\llm_client.cpp" ^
    "%SRC%\ui.cpp" ^
    "%SRC%\main.cpp" ^
    -lwinhttp -lws2_32 -lcrypt32 -ladvapi32 -lpdh -lole32 ^
    -o "%OUT%"

if %ERRORLEVEL% EQU 0 (
    echo.
    echo BUILD OK: %OUT%
) else (
    echo BUILD FAILED.
)

endlocal
pause
