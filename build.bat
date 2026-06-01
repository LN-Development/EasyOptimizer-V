@echo off
setlocal

where cl >nul 2>nul
if %errorlevel% neq 0 (
    echo [INFO] cl.exe not found. Attempting to find Visual Studio...
    set "VSCMD_START_DIR=%CD%"
    
    :: Try to find VS via vswhere
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "%VSWHERE%" (
        for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            set "VS_PATH=%%i"
        )
    )
    
    if defined VS_PATH (
        if exist "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" (
            call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
        )
    ) else (
        :: Hardcoded fallback paths
        if exist "D:\VS Studio\VC\Auxiliary\Build\vcvars64.bat" (
            call "D:\VS Studio\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
        ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
            call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
        )
    )
)

where cl >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] cl.exe not found even after trying to load vcvars64.bat.
    echo Please install Visual Studio with C++ desktop development tools, or run this from a Developer Command Prompt.
    pause
    exit /b 1
)

if not exist "build" mkdir build

set COMMON=/nologo /O2 /W3 /MD /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS
set CFLAGS=%COMMON% /I vendor\bc7enc_rdo
set CXXFLAGS=%COMMON% /EHsc /std:c++14 /DSUPPORT_BC7E=1

set LDFLAGS=/link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib shlwapi.lib msimg32.lib bcrypt.lib

set C_SOURCES=src\main.c src\theme.c src\hash.c src\ytd.c src\wtd.c src\dds.c src\texture.c src\image.c src\gui.c src\gui_cards.c src\ydr.c src\optimizer.c src\nvtt_c_wrapper.c

set CXX_SOURCES=src\bc7enc_wrapper.cpp vendor\bc7enc_rdo\bc7enc.cpp vendor\bc7enc_rdo\bc7decomp.cpp vendor\bc7enc_rdo\bc7decomp_ref.cpp vendor\bc7enc_rdo\rgbcx.cpp vendor\bc7enc_rdo\rdo_bc_encoder.cpp vendor\bc7enc_rdo\ert.cpp vendor\bc7enc_rdo\utils.cpp vendor\bc7enc_rdo\lodepng.cpp

set ISPC_OBJS=vendor\bc7enc_rdo\bc7e.obj vendor\bc7enc_rdo\bc7e_sse2.obj vendor\bc7enc_rdo\bc7e_sse4.obj vendor\bc7enc_rdo\bc7e_avx.obj vendor\bc7enc_rdo\bc7e_avx2.obj

echo [BUILD] Compiling C sources...
for %%f in (%C_SOURCES%) do (
    cl %CFLAGS% /c /Fo:build\ %%f
    if errorlevel 1 goto :fail
)

echo [BUILD] Compiling C++ sources (bc7enc_rdo + stb + wrapper)...
for %%f in (%CXX_SOURCES%) do (
    cl %CXXFLAGS% /c /Fo:build\ /I vendor\bc7enc_rdo /I vendor\stb %%f
    if errorlevel 1 goto :fail
)

echo [BUILD] Linking (with ISPC BC7 acceleration)...
cl /nologo /Fe:build\EasyOptimizer-V.exe build\*.obj %ISPC_OBJS% res\app.res %LDFLAGS%

if %errorlevel% equ 0 (
    echo [OK] build\EasyOptimizer-V.exe compilado com sucesso!
) else (
    goto :fail
)

goto :end

:fail
echo [FAIL] Build failed.
pause
exit /b 1

:end
pause
endlocal
