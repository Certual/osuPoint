@echo off
setlocal
chcp 65001 >nul

where cl.exe >nul 2>&1
if not errorlevel 1 goto toolchain_ready

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul

where cl.exe >nul 2>&1
if not errorlevel 1 goto toolchain_ready

if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if exist "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
if exist "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" call "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat" call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat" call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul

where cl.exe >nul 2>&1
if errorlevel 1 (
    echo.
    echo [ERROR] Visual Studio C++ x64 toolchain (cl.exe) not found.
    echo [ERROR] Please install MSVC or run from Developer Command Prompt.
    exit /b 1
)

:toolchain_ready

set CFLAGS_BASE=/nologo /O2 /Ot /Oi /Ob3 /GL /Gy /Gw /Oy /cgthreads1 /Brepro /Zc:inline /Zc:threadSafeInit- /Zc:tlsGuards- /Zc:nrvo /jumptablerdata /presetPadding- /favor:blend /guard:cf- /volatile:iso /std:c++20 /fp:fast /fp:except- /Gv /GA /Qfast_transcendentals /Qspectre- /sdl- /DNDEBUG /D_HAS_EXCEPTIONS=0 /D_DISABLE_VECTOR_ANNOTATION /D_ITERATOR_DEBUG_LEVEL=0 /GS- /GR- /EHs- /EHc- /EHa- /MT /utf-8 /W4 /WX /I . /I src
set LFLAGS=/link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF /GUARD:NO /CETCOMPAT:NO /DYNAMICBASE:NO /HIGHENTROPYVA:NO /INCREMENTAL:NO /FILEALIGN:4096 /STACK:0x100000,0x100000 /HEAP:0x100000,0x100000 gdiplus.lib comdlg32.lib /DEBUG:NONE /RELEASE
set SRCS_CORE=src\osuPoint.cpp src\cli.cpp src\log.cpp src\platform.cpp src\tablet_decoder.cpp src\driver_pipeline.cpp src\config_manager.cpp src\ui_view.cpp src\osuPoint.res

set STATUS_SSE=
set STATUS_AVX=
set STATUS_AVX2=
set STATUS_DEV=

if not "%1"=="" goto parse_arg

:menu
echo ==============================================================================
echo                 osu!Point Multi-Target Native Build System
echo ==============================================================================
echo.
echo   [1] osuPoint-sse.exe  : SSE2 Baseline (Universal / Legacy CPUs / VMs)
echo   [2] osuPoint-avx.exe  : AVX Baseline (Sandy Bridge / Ivy Bridge)
echo   [3] osuPoint.exe      : AVX2 Optimized (Haswell / Zen / Modern CPUs)
echo   [4] osuPoint-dev.exe  : Developer Binary with Built-in Selftest
echo   [5] ALL Targets       : Build and Package Full Production Suite
echo   [Q] Quit
echo.
echo ==============================================================================
set "TARGET_CHOICE="
set /p "TARGET_CHOICE=Select build target [1-5, Q]: "
if defined TARGET_CHOICE set "TARGET_CHOICE=%TARGET_CHOICE: =%"

if "%TARGET_CHOICE%"=="" (
    echo.
    echo [BUILD] No choice entered. Defaulting to ALL targets.
    goto do_all
)
if /I "%TARGET_CHOICE%"=="1" goto do_sse
if /I "%TARGET_CHOICE%"=="2" goto do_avx
if /I "%TARGET_CHOICE%"=="3" goto do_avx2
if /I "%TARGET_CHOICE%"=="4" goto do_dev
if /I "%TARGET_CHOICE%"=="5" goto do_all
if /I "%TARGET_CHOICE%"=="Q" exit /b 0

echo.
echo [ERROR] Invalid selection '%TARGET_CHOICE%'.
goto menu

:parse_arg
if /I "%1"=="1" goto do_sse
if /I "%1"=="SSE" goto do_sse
if /I "%1"=="2" goto do_avx
if /I "%1"=="AVX" goto do_avx
if /I "%1"=="3" goto do_avx2
if /I "%1"=="AVX2" goto do_avx2
if /I "%1"=="4" goto do_dev
if /I "%1"=="DEV" goto do_dev
if /I "%1"=="5" goto do_all
if /I "%1"=="ALL" goto do_all

:do_all
call :build_sse
call :build_avx
call :build_avx2
call :build_dev
goto print_summary

:do_sse
call :build_sse
goto print_summary

:do_avx
call :build_avx
goto print_summary

:do_avx2
call :build_avx2
goto print_summary

:do_dev
call :build_dev
goto print_summary

:build_sse
echo.
echo [BUILD] Compiling osuPoint-sse.exe [SSE2 Baseline]...
cl.exe %CFLAGS_BASE% /Fe:osuPoint-sse.exe %SRCS_CORE% %LFLAGS%
if errorlevel 1 (set STATUS_SSE=FAILED) else (set STATUS_SSE=SUCCESS)
exit /b 0

:build_avx
echo.
echo [BUILD] Compiling osuPoint-avx.exe [AVX Baseline]...
cl.exe %CFLAGS_BASE% /arch:AVX /Fe:osuPoint-avx.exe %SRCS_CORE% %LFLAGS%
if errorlevel 1 (set STATUS_AVX=FAILED) else (set STATUS_AVX=SUCCESS)
exit /b 0

:build_avx2
echo.
echo [BUILD] Compiling osuPoint.exe [AVX2 Optimized]...
cl.exe %CFLAGS_BASE% /arch:AVX2 /Fe:osuPoint.exe %SRCS_CORE% %LFLAGS%
if errorlevel 1 (
    set STATUS_AVX2=FAILED
) else (
    set STATUS_AVX2=SUCCESS
)
exit /b 0

:build_dev
echo.
echo [BUILD] Compiling osuPoint-dev.exe [Developer + Selftest]...
cl.exe %CFLAGS_BASE% /arch:AVX /DENABLE_SELFTEST=1 /Fe:osuPoint-dev.exe %SRCS_CORE% src\selftest.cpp %LFLAGS%
if errorlevel 1 (set STATUS_DEV=FAILED) else (set STATUS_DEV=SUCCESS)
exit /b 0

:print_summary
del /q *.obj 2>nul
echo.
echo ==============================================================================
echo BUILD SUMMARY
echo ==============================================================================
set "ANY_FAILED=0"
if "%STATUS_SSE%"=="SUCCESS" echo [SUCCESS] osuPoint-sse.exe  : Built [SSE2 Baseline]
if "%STATUS_SSE%"=="FAILED"  (echo [FAILED]  osuPoint-sse.exe  : Build failed & set "ANY_FAILED=1")
if "%STATUS_AVX%"=="SUCCESS" echo [SUCCESS] osuPoint-avx.exe  : Built [AVX Baseline]
if "%STATUS_AVX%"=="FAILED"  (echo [FAILED]  osuPoint-avx.exe  : Build failed & set "ANY_FAILED=1")
if "%STATUS_AVX2%"=="SUCCESS" echo [SUCCESS] osuPoint.exe      : Built [AVX2 Release]
if "%STATUS_AVX2%"=="FAILED"  (echo [FAILED]  osuPoint.exe      : Build failed & set "ANY_FAILED=1")
if "%STATUS_DEV%"=="SUCCESS" echo [SUCCESS] osuPoint-dev.exe  : Built [Dev + Selftest]
if "%STATUS_DEV%"=="FAILED"  (echo [FAILED]  osuPoint-dev.exe  : Build failed & set "ANY_FAILED=1")
echo ==============================================================================
if "%ANY_FAILED%"=="1" (
    endlocal
    exit /b 1
)
endlocal
exit /b 0
