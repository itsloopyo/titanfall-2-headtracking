@echo off
:: ============================================
:: Titanfall 2 Head Tracking - Uninstall
:: ============================================
:: Thin wrapper - uninstall body lives in cameraunlock-core/scripts/uninstall-body.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. To change
:: uninstall behaviour edit the body, not this wrapper. Everything below the
:: CONFIG BLOCK is copied verbatim from
:: cameraunlock-core/scripts/templates/uninstall-wrapper.cmd.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=titanfall-2"
set "MOD_DISPLAY_NAME=Titanfall 2 Head Tracking"
set "MOD_DLLS=Titanfall2HeadTracking.asi Titanfall2HeadTracking.log Titanfall2HeadTracking.prev.log"
set "MOD_INTERNAL_NAME=Titanfall2HeadTracking"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=ASILoader"
set "LEGACY_DLLS="
set "PLUGIN_SUBFOLDER="
set "MANAGED_SUBFOLDER="
set "ASSEMBLY_DLL="
set "PATCH_MARKER="
set "MANAGED_EXTRAS="
set "ASI_LOADER_NAME=dsound.dll"
set "UE4_BINARIES_RELDIR="
set "_SHIM=%SCRIPT_DIR%shared\find-game.ps1"
set "_SHIM_OUT=%TEMP%\cul-find-%RANDOM%-%RANDOM%.cmd"
set "_GIVEN_ARG="
set "_PS_EC=!errorlevel!"
set "REMOVE_LOADER=0"
set "DEPLOY_DIR=%GAME_PATH%\%MANAGED_SUBFOLDER%"
set "UE4_BINARIES_DIR=%GAME_PATH%\%UE4_BINARIES_RELDIR%"
set "REMOVED=0"
set "MANAGED_PATH=%GAME_PATH%\%MANAGED_SUBFOLDER%"
set "ASSEMBLY_PATH=%MANAGED_PATH%\%ASSEMBLY_DLL%"
set "BACKUP_PATH=%ASSEMBLY_PATH%.original"
set "_MARKER_CHECK=%SCRIPT_DIR%shared\cecil-marker-check.ps1"
set "MODS_TXT=!UE4_BINARIES_DIR!\Mods\mods.txt"
set "_MODS_TMP=%TEMP%\cul-modstxt-%RANDOM%-%RANDOM%.txt"
:: --- END CONFIG BLOCK ---

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\uninstall-body.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\uninstall-body.cmd"
if not exist "%_BODY%" (
    echo ERROR: uninstall-body.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
