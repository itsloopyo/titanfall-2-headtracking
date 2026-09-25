@echo off
:: ============================================
:: Titanfall 2 Head Tracking - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-asi.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. To change
:: install behaviour edit the body, not this wrapper. Everything below the
:: CONFIG BLOCK is copied verbatim from
:: cameraunlock-core/scripts/templates/install-wrapper-asi.cmd.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=titanfall-2"
set "MOD_DISPLAY_NAME=Titanfall 2 Head Tracking"
set "MOD_DLLS=Titanfall2HeadTracking.asi"
set "MOD_INTERNAL_NAME=Titanfall2HeadTracking"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=ASILoader"
set "ASI_LOADER_NAME=dsound.dll"
:: Files copied only when they are not already there, so an upgrade keeps
:: whatever the user tuned. Listing an .ini in MOD_DLLS instead puts it through
:: the unconditional copy and resets every key on every update.
set "MOD_SEED_FILES="
set "MOD_CONTROLS=Controls: End=toggle tracking, PgUp=cycle 6DOF/rotation/position, PgDn=toggle yaw mode. Chords: Ctrl+Shift+Y/G/H."
set "_SHIM=%SCRIPT_DIR%shared\find-game.ps1"
set "_SHIM_OUT=%TEMP%\cul-find-%RANDOM%-%RANDOM%.cmd"
set "_GIVEN_ARG="
set "_PS_EC=!errorlevel!"
set "WE_INSTALLED=true"
set "FILES_DIR=%SCRIPT_DIR%plugins"
set "DEPLOY_FAILED=1"
set "VENDOR_DIR=%SCRIPT_DIR%vendor\ultimate-asi-loader"
set "VENDOR_DLL=%VENDOR_DIR%\dinput8.dll"
:: Not used by this mod. Set blank so a value another mod's wrapper left in
:: the same console does not reach the body.
set "ASI_SUBDIR="
set "ASI_LOADER_VERSION="
:: --- END CONFIG BLOCK ---

:: Pin delayed expansion off before `%*` is expanded on the `call` below.
:: Under `cmd /V:ON`, or with DelayedExpansion=1 in
:: HKCU\Software\Microsoft\Command Processor, cmd.exe eats a `!` out of the
:: expanded line, and a real game path like C:\Games\Oh! My Game reaches the
:: body already mangled. The body pins expansion off at its own outer scope
:: too, but that is one `call` too late to save the argument it was handed.
setlocal disabledelayedexpansion

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-asi.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-asi.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-asi.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%