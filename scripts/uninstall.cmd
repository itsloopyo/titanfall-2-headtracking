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
:: Files install.cmd seeded write-if-absent. MUST list the same names as
:: install.cmd's MOD_SEED_FILES, or an uninstall leaves the mod's config behind.
set "MOD_SEED_FILES="
:: Config files the uninstall leaves in place so the player's settings survive a
:: reinstall: paths relative to the game folder, quoted when one holds a space.
:: Keep the line when it is blank, or the list another mod's uninstall.cmd set
:: in the same console is used instead.
set "PRESERVE_FILES="
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
:: Not used by this mod. Set blank so a value another mod's wrapper left in
:: the same console does not reach the body.
set "MOD_LEFTOVERS="
set "ROOT_EXTRAS="
set "USER_FOLDER_EXTRAS="
set "SHIM_MARKER="
set "SHIM_MARKER_ALT="
set "ASI_SUBDIR="
:: --- END CONFIG BLOCK ---

:: Pin delayed expansion off before `%*` is expanded on the `call` below.
:: Under `cmd /V:ON`, or with DelayedExpansion=1 in
:: HKCU\Software\Microsoft\Command Processor, cmd.exe eats a `!` out of the
:: expanded line, and a real game path like C:\Games\Oh! My Game reaches the
:: body already mangled. The body pins expansion off at its own outer scope
:: too, but that is one `call` too late to save the argument it was handed.
setlocal disabledelayedexpansion

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