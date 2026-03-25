@echo off
setlocal enabledelayedexpansion

echo ========================================
echo Building GAICA C++ Runner
echo ========================================
echo.

REM Ищем Visual Studio
set "VS_PATH="
set "VS_YEAR="

if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    set "VS_YEAR=2026"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    set "VS_YEAR=2022"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    set "VS_YEAR=2022"
) else if exist "C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
    set "VS_YEAR=2019"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
    set "VS_YEAR=2019"
) else (
    echo [ERROR] Visual Studio not found!
    echo.
    echo Please install Visual Studio with C++ support
    echo Download: https://visualstudio.microsoft.com/downloads/
    echo.
    pause
    exit /b 1
)

echo [OK] Found Visual Studio %VS_YEAR%
call "%VS_PATH%" >nul 2>&1

REM Проверяем компилятор
where cl.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Compiler not found after initialization!
    pause
    exit /b 1
)

echo [OK] Compiler ready
echo.

REM Переходим в папку проекта
cd /d "%~dp0"

REM Создаем папку bin
if not exist "bin" mkdir bin

REM Создаем папку web для статических файлов (если нет)
if not exist "web" mkdir web

echo Compiling sources...
echo.

cl.exe /std:c++17 /O2 /W3 /EHsc /Fe:bin\runner.exe ^
    src\main.cpp ^
    src\game_simulator.cpp ^
    src\bot_manager.cpp ^
    src\socket_server.cpp ^
    src\json_utils.cpp ^
    src\web_server.cpp ^
    src\logger.cpp ^
    src\dependency_checker.cpp ^
    /I include ^
    /D_WIN32_WINNT=0x0601 ^
    /D_CRT_SECURE_NO_WARNINGS ^
    ws2_32.lib wsock32.lib

if errorlevel 1 (
    echo.
    echo ========================================
    echo BUILD FAILED!
    echo ========================================
    pause
    exit /b 1
)

echo.
echo ========================================
echo BUILD SUCCESSFUL!
echo ========================================
echo.
echo Executable: %cd%\bin\runner.exe
echo.
echo To run a match:
echo   bin\runner.exe --bot-a "C:\KAZAN\bot\Osnova" --bot-b "C:\KAZAN\bot\TestsBots\my_smurf_bot_v10" --rounds 3
echo.
echo To run with web visualization:
echo   bin\runner.exe --bot-a "C:\KAZAN\bot\Osnova" --bot-b "C:\KAZAN\bot\TestsBots\my_smurf_bot_v10" --web-port 8080
echo.
echo To check bot dependencies:
echo   python check_bot.py "C:\KAZAN\bot\TestsBots\my_smurf_bot_v10"
echo.