@echo off
setlocal
chcp 65001 >nul

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

set CFLAGS_COMMON=/nologo /O2 /Ot /Oi /Ob3 /GL /Gy /Gw /Oy /cgthreads1 /Brepro /Zc:inline /Zc:threadSafeInit- /Zc:tlsGuards- /Zc:nrvo /jumptablerdata /presetPadding- /favor:blend /guard:cf- /volatile:iso /std:c++20 /fp:fast /fp:except- /Gv /GA /Qfast_transcendentals /Qspectre- /sdl- /DNDEBUG /D_HAS_EXCEPTIONS=0 /D_DISABLE_VECTOR_ANNOTATION /D_ITERATOR_DEBUG_LEVEL=0 /GS- /GR- /EHs- /EHc- /EHa- /MT /utf-8 /W4 /WX /I . /I src
set LFLAGS=/link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF /MERGE:.rdata=.text /GUARD:NO /CETCOMPAT:NO /DYNAMICBASE:NO /HIGHENTROPYVA:NO /INCREMENTAL:NO /FILEALIGN:4096 /STACK:0x100000,0x100000 /HEAP:0x100000,0x100000 gdiplus.lib comdlg32.lib /DEBUG:NONE /RELEASE
set SRCS_COMMON=src\platform.cpp src\tablet_decoder.cpp src\driver_pipeline.cpp src\config_manager.cpp src\ui_view.cpp src\osuPoint.cpp src\osuPoint.res

set STATUS_AVX=
set STATUS_NOAVX=
set STATUS_DEV=

if defined CI goto do_all
if defined BUILD_AUTOMATION goto do_all

if "%1"=="1" goto do_release
if "%1"=="release" goto do_release
if "%1"=="avx" goto do_release
if "%1"=="avx2" goto do_release

if "%1"=="2" goto do_noavx
if "%1"=="noavx" goto do_noavx
if "%1"=="sse" goto do_noavx
if "%1"=="sse2" goto do_noavx

if "%1"=="3" goto do_dev
if "%1"=="dev" goto do_dev
if "%1"=="selftest" goto do_dev

if "%1"=="4" goto do_all
if "%1"=="all" goto do_all

:show_menu
echo.
echo ==============================================================================
echo                          osu!Point  ^|  Build System
echo                      Ultra-Low Latency Direct-HID Driver
echo ==============================================================================
echo.
echo   Select a target configuration to compile:
echo.
echo   [1]  AVX2 Release            (Recommended for Modern CPUs)
echo        - Target   : osuPoint.exe
echo        - ISA      : AVX2 + FMA vector pipeline
echo        - Purpose  : Competitive gameplay, lowest coordinate transform jitter
echo        - Requires : Intel Haswell (4th Gen+) or AMD Zen (Ryzen+)
echo.
echo   [2]  SSE2 Compatibility      (Legacy / Broad Support)
echo        - Target   : osuPoint-noavx.exe
echo        - ISA      : Baseline x86-64 SSE2 (Non-AVX)
echo        - Purpose  : Universal compatibility for older systems, VMs, or Celerons
echo        - Requires : Standard 64-bit x86-64 processor
echo.
echo   [3]  Developer + Selftest    (Diagnostics and Regression Suite)
echo        - Target   : osuPoint-dev.exe
echo        - Features : Built-in --selftest harness (V1-V21 golden decode vectors)
echo        - Purpose  : Protocol verification, debug logging, and unit checks
echo.
echo   [4]  Build All Targets       (Full Distribution Package)
echo        - Compiles all configurations sequentially ([1], [2], and [3])
echo        - Generates complete binary release set
echo.
echo ==============================================================================
choice /C 1234 /N /T 5 /D 4 /M "  Enter selection [1-4] (Auto-selects [4] in 5s): "
set "CHOICE=%ERRORLEVEL%"

if "%CHOICE%"=="1" goto do_release
if "%CHOICE%"=="2" goto do_noavx
if "%CHOICE%"=="3" goto do_dev
if "%CHOICE%"=="4" goto do_all

echo.
echo   Invalid selection (%CHOICE%). Building all targets by default...
echo.
goto do_all

:do_all
call :build_release
call :build_noavx
call :build_dev
goto print_summary

:do_release
call :build_release
if "%STATUS_AVX%"=="SKIPPED" call :build_noavx
goto print_summary

:do_noavx
call :build_noavx
goto print_summary

:do_dev
call :build_dev
goto print_summary

:build_release
echo.
echo ------------------------------------------------------------------------------
echo   Compiling: osuPoint.exe [AVX2 Release]
echo ------------------------------------------------------------------------------
cl.exe %CFLAGS_COMMON% /arch:AVX2 /Fe:osuPoint.exe %SRCS_COMMON% %LFLAGS%
if errorlevel 1 (
    echo   [SKIP] AVX2 compilation failed or instruction set unsupported.
    set STATUS_AVX=SKIPPED
) else (
    set STATUS_AVX=SUCCESS
)
exit /b 0

:build_noavx
echo.
echo ------------------------------------------------------------------------------
echo   Compiling: osuPoint-noavx.exe [SSE2 Baseline]
echo ------------------------------------------------------------------------------
cl.exe %CFLAGS_COMMON% /Fe:osuPoint-noavx.exe %SRCS_COMMON% %LFLAGS%
if errorlevel 1 (
    echo   [FAIL] Compilation failed for osuPoint-noavx.exe!
    set STATUS_NOAVX=FAILED
) else (
    set STATUS_NOAVX=SUCCESS
)
exit /b 0

:build_dev
echo.
echo ------------------------------------------------------------------------------
echo   Compiling: osuPoint-dev.exe [Developer + Selftest]
echo ------------------------------------------------------------------------------
    cl.exe %CFLAGS_COMMON% /DENABLE_SELFTEST=1 /Fe:osuPoint-dev.exe %SRCS_COMMON% src\selftest.cpp %LFLAGS%
if errorlevel 1 (
    echo   [FAIL] Compilation failed for osuPoint-dev.exe!
    set STATUS_DEV=FAILED
) else (
    set STATUS_DEV=SUCCESS
)
exit /b 0

:print_summary
echo.
echo ==============================================================================
echo                               BUILD SUMMARY
echo ==============================================================================
if "%STATUS_AVX%"=="SUCCESS"   echo   [SUCCESS]  osuPoint.exe        : Built successfully [AVX2]
if "%STATUS_AVX%"=="SKIPPED"   echo   [SKIPPED]  osuPoint.exe        : Skipped (AVX2 unsupported)
if "%STATUS_AVX%"=="FAILED"    echo   [FAILED]   osuPoint.exe        : Build failed

if "%STATUS_NOAVX%"=="SUCCESS" echo   [SUCCESS]  osuPoint-noavx.exe  : Built successfully [SSE2]
if "%STATUS_NOAVX%"=="FAILED"  echo   [FAILED]   osuPoint-noavx.exe  : Build failed

if "%STATUS_DEV%"=="SUCCESS"   echo   [SUCCESS]  osuPoint-dev.exe    : Built successfully [Dev / Selftest]
if "%STATUS_DEV%"=="FAILED"    echo   [FAILED]   osuPoint-dev.exe    : Build failed

if "%STATUS_AVX%"=="SKIPPED" (
    echo.
    echo   Notice: AVX2 instructions are unsupported on this CPU/toolchain.
    echo   Use osuPoint-noavx.exe for standard operation.
)
echo ==============================================================================
echo.

endlocal
