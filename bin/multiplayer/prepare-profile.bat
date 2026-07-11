@echo off
setlocal EnableExtensions

if "%~1"=="" (
    echo Usage: %~nx0 ^<profile-directory^>
    exit /b 2
)

for %%I in ("%~dp0..") do set "BIN_DIR=%%~fI"
for %%I in ("%~1") do set "PROFILE_DIR=%%~fI"

if not exist "%PROFILE_DIR%" md "%PROFILE_DIR%"
if not exist "%PROFILE_DIR%\nvram" md "%PROFILE_DIR%\nvram"
if not exist "%PROFILE_DIR%\logs" md "%PROFILE_DIR%\logs"

call :link_dir "%PROFILE_DIR%\CBE" "%BIN_DIR%\CBE" || exit /b 1
call :link_dir "%PROFILE_DIR%\JHOnlineData" "%BIN_DIR%\JHOnlineData" || exit /b 1

for %%F in (font_16_sky.uc3 font_gb.uc3 gb16.uc2.uc3 updatetk42.dat) do (
    if not exist "%PROFILE_DIR%\%%F" copy /y "%BIN_DIR%\%%F" "%PROFILE_DIR%\%%F" >nul || exit /b 1
)

exit /b 0

:link_dir
if exist "%~1" exit /b 0
mklink /J "%~1" "%~2" >nul
if errorlevel 1 (
    echo Failed to create resource junction: %~1
    exit /b 1
)
exit /b 0
