@echo off
:: ============================================
:: CameraUnlock Uninstall Template (Unified)
:: ============================================
:: Source of truth: cameraunlock-core/scripts/templates/uninstall.cmd.
:: Copy to <mod>/scripts/uninstall.cmd, edit CONFIG BLOCK, leave the rest
:: alone. Contract: see ~/.claude/CLAUDE.md "install.cmd / uninstall.cmd
:: - Unified Launcher Contract".
::
:: One template, all loader variants. Dispatch is by FRAMEWORK_TYPE which
:: MUST match what install.cmd wrote to the state file. Supported values:
::
::   BepInEx      - removes <game>/BepInEx/, winhttp.dll, doorstop files
::   MelonLoader  - removes <game>/MelonLoader/, version.dll, dobby.dll
::   MonoCecil    - restores Assembly-CSharp.dll from .original backup
::   ASILoader    - removes <exe-dir>/winmm.dll (or dinput8.dll)
::   REFramework  - removes <game>/dinput8.dll and <game>/reframework/
::   UE4SS        - removes <win64>/Mods/<ModName>/ and its mods.txt entry;
::                  UE4SS.dll + dwmapi.dll only if we installed the loader
::   None         - shim-only; restores shim DLLs from .backup if present
::
:: Launcher CLI: uninstall.cmd [GAME_PATH] [/y] [/force]
::   /y      - non-interactive; skip every pause and prompt
::   /force  - remove loader even if state says installed_by_us=false
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=titanfall-2"
set "MOD_DISPLAY_NAME=Titanfall 2 Head Tracking"
set "MOD_DLLS=Titanfall2HeadTracking.asi Titanfall2HeadTracking.log Titanfall2HeadTracking.prev.log"
set "MOD_INTERNAL_NAME=Titanfall2HeadTracking"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=ASILoader"
set "LEGACY_DLLS="

:: --- Loader-specific config (leave the ones that don't apply blank) ---
:: MonoCecil: used to find + restore the original Assembly-CSharp.dll.
set "MANAGED_SUBFOLDER="
set "ASSEMBLY_DLL="
:: MonoCecil REQUIRED: the patcher's marker (must match install.cmd /
:: BootstrapPatcher.cs). The uninstall body refuses to restore a patched
:: .original over the game assembly. Leave blank for non-MonoCecil mods.
set "PATCH_MARKER="
:: MonoCecil: extra files to also remove from MANAGED_SUBFOLDER (config/log
:: files left behind by the mod itself).
set "MANAGED_EXTRAS="
:: ASILoader: filename the ASI DLL was renamed to. Titanfall 2 uses dsound.dll.
set "ASI_LOADER_NAME=dsound.dll"
:: UE4SS REQUIRED: path under GAME_PATH holding the shipping exe. MUST match
:: install.cmd's UE4_BINARIES_RELDIR. Mods live in its Mods\ subfolder.
set "UE4_BINARIES_RELDIR="
:: --- END CONFIG BLOCK ---

call :detect_yes_flag %*
:: :detect_yes_flag and the arg parser both break if the shell left delayed
:: expansion on - cmd /V:ON, or DelayedExpansion=1 under
:: HKCU\Software\Microsoft\Command Processor. Under either, a "!" in the game
:: path is eaten out of the expanded line before the parser ever compares it, and
:: a real directory is rejected as a malformed argument. Moving the enable to
:: after :args_done is not enough on its own; the default has to be pinned OFF.
setlocal disabledelayedexpansion

call :main %*
set "_EC=%errorlevel%"
if not defined YES_FLAG ( echo. & pause )
exit /b %_EC%

:: ============================================
:: Pre-scan args at outer scope so YES_FLAG propagates to the post-:main
:: pause check. :main's arg parser sets its own (local) YES_FLAG too, but
:: cmd.exe discards local vars when setlocal pops on `exit /b`, so without
:: this pre-scan the post-:main `if not defined YES_FLAG` always pauses
:: and /y can't make the script headless. Quoted-string form is required
:: here - bracket form `if [%~1]==[/y]` does NOT quote, so a path arg
:: containing whitespace ("C:\...\Gone Home") splits across the brackets
:: and crashes cmd with "[Home]==[/y] was unexpected at this time". The
:: trailing-backslash hazard the bracket form was working around is moot
:: with `%~1`: it strips the launcher's surrounding quotes before the
:: comparison, so a value like `C:\foo\` can't escape the closing `"`.
:: ============================================
:detect_yes_flag
if "%~1"=="" exit /b 0
if /i "%~1"=="/y"    set "YES_FLAG=1"
if /i "%~1"=="-y"    set "YES_FLAG=1"
if /i "%~1"=="--yes" set "YES_FLAG=1"
shift
goto :detect_yes_flag

:main

:: Capture script dir BEFORE the arg parser runs. Inside `call :main`,
:: `shift` rotates %0 too, so %~dp0 read after shifts resolves to the
:: dirname of the first arg (e.g. C:\ for /y) instead of the script.
set "SCRIPT_DIR=%~dp0"

:: -------- Arg parser (canonical, do not modify) --------
:: Parsed with delayed expansion OFF; `setlocal enabledelayedexpansion`
:: deliberately comes after :args_done. With it on, cmd strips `!` out of the
:: expanded text of `set "_ARG=%~1"` - and out of `%~1` itself - so a real
:: game path like C:\Games\Oh! My Game silently loses the `!`, `if exist`
:: fails, and a valid directory is rejected as a malformed argument.
set "YES_FLAG="
set "FORCE_FLAG="
set "_GIVEN_PATH="
:parse_args
if "%~1"=="" goto :args_done
set "_ARG=%~1"
if /i "%_ARG%"=="/y"      ( set "YES_FLAG=1"   & shift & goto :parse_args )
if /i "%_ARG%"=="-y"      ( set "YES_FLAG=1"   & shift & goto :parse_args )
if /i "%_ARG%"=="--yes"   ( set "YES_FLAG=1"   & shift & goto :parse_args )
if /i "%_ARG%"=="/force"  ( set "FORCE_FLAG=1" & shift & goto :parse_args )
if /i "%_ARG%"=="--force" ( set "FORCE_FLAG=1" & shift & goto :parse_args )
if "%_ARG:~0,2%"=="--" ( echo ERROR: unknown flag "%_ARG%" & exit /b 2 )
if "%_ARG:~0,1%"=="/"  ( echo ERROR: unknown flag "%_ARG%" & exit /b 2 )
if "%_ARG:~0,1%"=="-"  ( echo ERROR: unknown flag "%_ARG%" & exit /b 2 )
if not defined _GIVEN_PATH (
    if exist "%_ARG%\" ( set "_GIVEN_PATH=%_ARG%" & shift & goto :parse_args )
)
echo ERROR: unrecognised argument "%_ARG%"
exit /b 2
:args_done
set "_ARG="

setlocal enabledelayedexpansion

:: -------- Validate CONFIG BLOCK --------
:: Every name below is interpolated straight into a path that gets written,
:: deleted or recursively removed. A blank one does not fail - it silently
:: retargets the operation at the parent directory, which is the game folder.
for %%v in (GAME_ID MOD_DISPLAY_NAME STATE_FILE FRAMEWORK_TYPE) do (
    if not defined %%v (
        echo ERROR: %%v is not set in this script's CONFIG BLOCK.
        exit /b 1
    )
)

echo.
echo === %MOD_DISPLAY_NAME% - Uninstall ===
echo.

:: -------- Resolve game path via shared shim --------
set "_SHIM=%SCRIPT_DIR%shared\find-game.ps1"
if not exist "%_SHIM%" set "_SHIM=%SCRIPT_DIR%..\cameraunlock-core\scripts\find-game.ps1"
if not exist "%_SHIM%" (
    echo ERROR: find-game.ps1 not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, make sure the cameraunlock-core submodule is checked out.
    exit /b 1
)
set "_SHIM_OUT=%TEMP%\cul-find-%RANDOM%-%RANDOM%.cmd"
set "_GIVEN_ARG="
if defined _GIVEN_PATH set "_GIVEN_ARG=-GivenPath "!_GIVEN_PATH!""
powershell -NoProfile -ExecutionPolicy Bypass -File "%_SHIM%" -GameId %GAME_ID% -OutFile "!_SHIM_OUT!" !_GIVEN_ARG!
set "_PS_EC=!errorlevel!"
if not "!_PS_EC!"=="0" (
    echo.
    echo ERROR: Could not resolve game install path ^(shim exit code !_PS_EC!^).
    echo Pass a path explicitly: uninstall.cmd "C:\path\to\game"
    echo.
    del "!_SHIM_OUT!" 2>nul
    exit /b 1
)
call "!_SHIM_OUT!"
del "!_SHIM_OUT!" 2>nul

echo Game found: "%GAME_PATH%"
echo.

:: -------- Game-running check --------
tasklist /fi "imagename eq %GAME_EXE%" 2>nul | findstr /i "%GAME_EXE%" >nul 2>&1
if not errorlevel 1 (
    echo ERROR: %GAME_DISPLAY_NAME% is currently running.
    echo Please close the game before uninstalling.
    echo.
    exit /b 1
)

:: -------- Compute DEPLOY_DIR per FRAMEWORK_TYPE --------
call :compute_deploy_dir
if errorlevel 1 exit /b 1

:: -------- Remove mod files (framework-aware) --------
if /i "%FRAMEWORK_TYPE%"=="None" (
    call :remove_shim_files
) else if /i "%FRAMEWORK_TYPE%"=="UE4SS" (
    rem The mod folder and its mods.txt line are ours whoever installed the
    rem loader, so they come off here, not in the installed_by_us-gated step.
    call :remove_ue4ss_mod
    if errorlevel 1 exit /b 1
) else if /i "%FRAMEWORK_TYPE%"=="MonoCecil" (
    rem Cecil: restore backup THEN remove our DLLs from Managed/.
    call :remove_MonoCecil
    if errorlevel 1 exit /b 1
    call :remove_mod_files_plain
    call :remove_managed_extras
) else (
    call :remove_mod_files_plain
)

:: -------- Decide whether to remove loader --------
set "REMOVE_LOADER=0"
if "!FORCE_FLAG!"=="1" set "REMOVE_LOADER=1"
if "!REMOVE_LOADER!"=="0" (
    if exist "%GAME_PATH%\%STATE_FILE%" (
        findstr /c:"installed_by_us" "%GAME_PATH%\%STATE_FILE%" 2>nul | findstr /c:"true" >nul 2>&1
        if not errorlevel 1 set "REMOVE_LOADER=1"
    )
)

if /i "%FRAMEWORK_TYPE%"=="None" (
    rem Shim-only: already handled in :remove_shim_files above (restores .backup).
    rem
) else if /i "%FRAMEWORK_TYPE%"=="MonoCecil" (
    rem Cecil: the backup restore IS the loader removal. Already done.
    rem
) else (
    if "!REMOVE_LOADER!"=="1" (
        echo.
        if "!FORCE_FLAG!"=="1" (
            echo Removing %FRAMEWORK_TYPE% ^(/force^)...
        ) else (
            echo Removing %FRAMEWORK_TYPE% ^(installed by this mod^)...
        )
        call :remove_%FRAMEWORK_TYPE%
    ) else (
        echo.
        echo %FRAMEWORK_TYPE% was not installed by this mod - leaving intact. Use /force to remove anyway.
    )
)

:: -------- Remove state file --------
if exist "%GAME_PATH%\%STATE_FILE%" (
    del "%GAME_PATH%\%STATE_FILE%"
    echo   Removed: state file
)

echo.
echo === Uninstall Complete ===
echo.
exit /b 0

:: ============================================
:: compute_deploy_dir: set DEPLOY_DIR based on FRAMEWORK_TYPE.
:: For ASILoader and None, DEPLOY_DIR is derived from the shim's
:: GAME_EXE_RELPATH (so nested-exe games like DL2 work).
:: ============================================
:compute_deploy_dir
if /i "%FRAMEWORK_TYPE%"=="BepInEx" (
    set "DEPLOY_DIR=%GAME_PATH%\BepInEx\plugins"
    exit /b 0
)
if /i "%FRAMEWORK_TYPE%"=="MelonLoader" (
    set "DEPLOY_DIR=%GAME_PATH%\Mods"
    exit /b 0
)
if /i "%FRAMEWORK_TYPE%"=="REFramework" (
    set "DEPLOY_DIR=%GAME_PATH%\reframework\plugins"
    exit /b 0
)
if /i "%FRAMEWORK_TYPE%"=="UE4SS" (
    if not defined UE4_BINARIES_RELDIR (
        echo ERROR: UE4_BINARIES_RELDIR is not set in the uninstall CONFIG BLOCK.
        echo It must match the value install.cmd used.
        exit /b 1
    )
    if not defined MOD_INTERNAL_NAME (
        echo ERROR: MOD_INTERNAL_NAME is not set in the uninstall CONFIG BLOCK.
        echo Without it the mod folder path resolves to the whole Mods\ tree.
        exit /b 1
    )
    set "UE4_BINARIES_DIR=%GAME_PATH%\%UE4_BINARIES_RELDIR%"
    set "DEPLOY_DIR=%GAME_PATH%\%UE4_BINARIES_RELDIR%\Mods\%MOD_INTERNAL_NAME%"
    exit /b 0
)
if /i "%FRAMEWORK_TYPE%"=="MonoCecil" (
    set "DEPLOY_DIR=%GAME_PATH%\%MANAGED_SUBFOLDER%"
    exit /b 0
)
if /i "%FRAMEWORK_TYPE%"=="ASILoader" (
    for %%i in ("%GAME_PATH%\%GAME_EXE_RELPATH%") do set "DEPLOY_DIR=%%~dpi"
    if "!DEPLOY_DIR:~-1!"=="\" set "DEPLOY_DIR=!DEPLOY_DIR:~0,-1!"
    exit /b 0
)
if /i "%FRAMEWORK_TYPE%"=="None" (
    for %%i in ("%GAME_PATH%\%GAME_EXE_RELPATH%") do set "DEPLOY_DIR=%%~dpi"
    if "!DEPLOY_DIR:~-1!"=="\" set "DEPLOY_DIR=!DEPLOY_DIR:~0,-1!"
    exit /b 0
)
echo ERROR: Unknown FRAMEWORK_TYPE "%FRAMEWORK_TYPE%" in uninstall CONFIG BLOCK.
exit /b 1

:: ============================================
:: Remove mod DLLs + legacy DLLs from DEPLOY_DIR (framework-generic).
:: ============================================
:remove_mod_files_plain
echo Removing mod files...
set "REMOVED=0"
for %%f in (%MOD_DLLS%) do (
    if exist "!DEPLOY_DIR!\%%f" (
        del "!DEPLOY_DIR!\%%f"
        echo   Removed: %%f
        set /a REMOVED+=1
    )
)
if defined LEGACY_DLLS (
    for %%f in (%LEGACY_DLLS%) do (
        if exist "!DEPLOY_DIR!\%%f" (
            del "!DEPLOY_DIR!\%%f"
            echo   Removed: %%f ^(legacy^)
            set /a REMOVED+=1
        )
    )
)
if "!REMOVED!"=="0" echo   No mod files found
exit /b 0

:: ============================================
:: Remove extra files Cecil mods leave in Managed/ (configs, logs, etc.).
:: ============================================
:remove_managed_extras
if not defined MANAGED_EXTRAS exit /b 0
for %%f in (%MANAGED_EXTRAS%) do (
    if exist "!DEPLOY_DIR!\%%f" (
        del "!DEPLOY_DIR!\%%f"
        echo   Removed: %%f
    )
)
exit /b 0

:: ============================================
:: Remove shim DLLs - restore <name>.backup if present so the user's
:: pre-mod state comes back. Also handles any LEGACY_DLLS list entries.
:: ============================================
:remove_shim_files
echo Removing shim files...
set "REMOVED=0"
for %%f in (%MOD_DLLS%) do (
    if exist "!DEPLOY_DIR!\%%f.backup" (
        if exist "!DEPLOY_DIR!\%%f" del /q "!DEPLOY_DIR!\%%f" >nul 2>&1
        move /y "!DEPLOY_DIR!\%%f.backup" "!DEPLOY_DIR!\%%f" >nul
        echo   Restored original %%f from backup
        set /a REMOVED+=1
    ) else (
        if exist "!DEPLOY_DIR!\%%f" (
            del "!DEPLOY_DIR!\%%f"
            echo   Removed: %%f ^(no backup was present^)
            set /a REMOVED+=1
        )
    )
)
if defined LEGACY_DLLS (
    for %%f in (%LEGACY_DLLS%) do (
        if exist "!DEPLOY_DIR!\%%f" (
            del "!DEPLOY_DIR!\%%f"
            echo   Removed: %%f ^(legacy^)
            set /a REMOVED+=1
        )
    )
)
if "!REMOVED!"=="0" echo   No shim files found
exit /b 0

:: ============================================
:: Remove BepInEx (regular and BepInExPack both land in the same layout).
:: ============================================
:remove_BepInEx
if exist "%GAME_PATH%\BepInEx" (
    rmdir /s /q "%GAME_PATH%\BepInEx"
    echo   Removed: BepInEx folder
)
for %%f in (winhttp.dll doorstop_config.ini .doorstop_version changelog.txt) do (
    if exist "%GAME_PATH%\%%f" (
        del "%GAME_PATH%\%%f"
        echo   Removed: %%f
    )
)
exit /b 0

:: ============================================
:: Remove MelonLoader. Only delete Mods/UserLibs/UserData if empty
:: (mod-file removal above may leave them clean; users with other
:: melon mods installed keep their data).
:: ============================================
:remove_MelonLoader
if exist "%GAME_PATH%\MelonLoader" (
    rmdir /s /q "%GAME_PATH%\MelonLoader"
    echo   Removed: MelonLoader folder
)
for %%f in (version.dll dobby.dll NOTICE.txt) do (
    if exist "%GAME_PATH%\%%f" (
        del "%GAME_PATH%\%%f"
        echo   Removed: %%f
    )
)
for %%d in (Mods UserLibs UserData) do (
    if exist "%GAME_PATH%\%%d" (
        dir /b /a "%GAME_PATH%\%%d" 2>nul | findstr /r /v "^$" >nul
        if errorlevel 1 (
            rmdir "%GAME_PATH%\%%d" 2>nul
            if not exist "%GAME_PATH%\%%d" echo   Removed: %%d\ ^(empty^)
        )
    )
)
exit /b 0

:: ============================================
:: Mono.Cecil: restore Assembly-CSharp.dll from the .original backup.
:: The mod DLLs in Managed/ are cleaned up separately by the plain loop.
:: ============================================
:remove_MonoCecil
set "MANAGED_PATH=%GAME_PATH%\%MANAGED_SUBFOLDER%"
set "ASSEMBLY_PATH=%MANAGED_PATH%\%ASSEMBLY_DLL%"
set "BACKUP_PATH=%ASSEMBLY_PATH%.original"
:: The .original must be pristine: never restore a patched backup over the
:: game assembly, and never strip the mod DLLs while leaving a patched
:: assembly that can no longer find them. PATCH_MARKER drives the check.
if not defined PATCH_MARKER (
    echo   ERROR: PATCH_MARKER is not set in the uninstall.cmd CONFIG BLOCK.
    echo   Cannot verify assembly patch state; aborting.
    exit /b 1
)
if not exist "%BACKUP_PATH%" (
    rem No backup. Safe only if the live assembly is already clean; otherwise
    rem removing the mod DLLs would orphan a patched assembly.
    call :cecil_marker_state "%ASSEMBLY_PATH%"
    if errorlevel 2 ( echo   ERROR: could not verify %ASSEMBLY_DLL% patch state. & exit /b 1 )
    if errorlevel 1 ( echo   No backup, and %ASSEMBLY_DLL% is already clean - nothing to restore. & exit /b 0 )
    echo   ERROR: %ASSEMBLY_DLL% is patched but no .original backup exists.
    echo   Run Steam "Verify integrity of game files" to restore a clean assembly, then re-run uninstall.
    exit /b 1
)
call :cecil_marker_state "%BACKUP_PATH%"
if errorlevel 2 ( echo   ERROR: could not read %ASSEMBLY_DLL%.original to verify it is pristine. & exit /b 1 )
if errorlevel 1 goto :_cecil_restore
echo   ERROR: %ASSEMBLY_DLL%.original is patched - corrupt backup, not restoring.
echo   Delete it and run Steam "Verify integrity of game files" to restore a clean %ASSEMBLY_DLL%.
exit /b 1
:_cecil_restore
copy /y "%BACKUP_PATH%" "%ASSEMBLY_PATH%" >nul
del "%BACKUP_PATH%"
echo   Restored: %ASSEMBLY_DLL% from backup
exit /b 0

:: ============================================
:: Resolve the marker-check helper and report whether %~1 is patched.
:: Returns errorlevel 0 = patched, 1 = pristine, 2 = error. Requires
:: PATCH_MARKER. Kept as its own routine so the errorlevel reads stay outside
:: parenthesised blocks where %errorlevel% would expand too early.
:: ============================================
:cecil_marker_state
set "_MARKER_CHECK=%SCRIPT_DIR%shared\cecil-marker-check.ps1"
if not exist "%_MARKER_CHECK%" set "_MARKER_CHECK=%SCRIPT_DIR%..\cameraunlock-core\scripts\cecil-marker-check.ps1"
if not exist "%_MARKER_CHECK%" exit /b 2
powershell -NoProfile -ExecutionPolicy Bypass -File "%_MARKER_CHECK%" -AssemblyPath "%~1" -Marker "%PATCH_MARKER%"
exit /b %errorlevel%

:: ============================================
:: Remove Ultimate ASI Loader from EXE_DIR.
:: ============================================
:remove_ASILoader
for %%i in ("%GAME_PATH%\%GAME_EXE_RELPATH%") do set "EXE_DIR=%%~dpi"
if "!EXE_DIR:~-1!"=="\" set "EXE_DIR=!EXE_DIR:~0,-1!"
:: Only the proxy this package actually installed. Sweeping the other common
:: ASI names off the disk deletes OTHER software's loader: winmm.dll and
:: dinput8.dll are what ReShade and most other ASI mods proxy through, so
:: uninstalling this mod used to silently break them.
:: Guarded because an unset ASI_LOADER_NAME collapses the path to the exe
:: DIRECTORY, and deleting a bare directory path expands to every file in it
:: and prompts "Are you sure (Y/N)?" - which under /y has no answer and blocks
:: on stdin forever. The CONFIG BLOCK documents this name as optional with a
:: default, but the body never applied one.
if not defined ASI_LOADER_NAME (
    echo   ERROR: ASI_LOADER_NAME is not set in the uninstall.cmd CONFIG BLOCK.
    echo   Cannot tell which proxy DLL belongs to this mod; refusing to guess.
    exit /b 1
)
if exist "!EXE_DIR!\%ASI_LOADER_NAME%" (
    del "!EXE_DIR!\%ASI_LOADER_NAME%"
    echo   Removed: %ASI_LOADER_NAME%
)
exit /b 0

:: ============================================
:: Remove REFramework.
:: ============================================
:remove_REFramework
if exist "%GAME_PATH%\dinput8.dll" (
    del "%GAME_PATH%\dinput8.dll"
    echo   Removed: dinput8.dll
)
if exist "%GAME_PATH%\reframework" (
    rmdir /s /q "%GAME_PATH%\reframework"
    echo   Removed: reframework/
)
:: Loose files REFramework's zip drops at the game root: the revision marker,
:: plus VR runtime DLLs the install stripped for flatscreen mode (clean up any
:: an older install left behind) so uninstall returns the game to vanilla.
for %%f in (reframework_revision.txt openvr_api.dll openxr_loader.dll DELETE_OPENVR_API_DLL_IF_YOU_WANT_TO_USE_OPENXR) do (
    if exist "%GAME_PATH%\%%f" (
        del /q "%GAME_PATH%\%%f" >nul 2>&1
        echo   Removed: %%f
    )
)
exit /b 0

:: ============================================
:: Remove the UE4SS Lua mod: its folder under Win64\Mods\ and its line in
:: Win64\Mods\mods.txt. Runs regardless of who installed the loader - only
:: the loader DLLs themselves are gated on installed_by_us.
:: ============================================
:remove_ue4ss_mod
echo Removing mod files...
if exist "!DEPLOY_DIR!\" (
    rmdir /s /q "!DEPLOY_DIR!"
    echo   Removed: Mods\%MOD_INTERNAL_NAME%\
) else (
    echo   No mod folder found
)
set "MODS_TXT=!UE4_BINARIES_DIR!\Mods\mods.txt"
if not exist "!MODS_TXT!" exit /b 0
:: Matched as "NAME :" rather than "NAME ". A bare trailing space also matches a
:: LONGER name that merely starts the same way, so uninstalling "HeadTracking"
:: would have deregistered "HeadTracking Extras" as well - and UE4SS mod names
:: come from folder names, which may contain spaces.
findstr /b /c:"%MOD_INTERNAL_NAME% :" "!MODS_TXT!" >nul
:: 1 = not present (nothing to do). 2+ = the file could not be READ, which is
:: not the same thing and must not be reported as a clean deregistration.
if errorlevel 2 (
    echo   ERROR: could not read !MODS_TXT! to deregister %MOD_INTERNAL_NAME%.
    exit /b 1
)
if errorlevel 1 exit /b 0
set "_MODS_TMP=%TEMP%\cul-modstxt-%RANDOM%-%RANDOM%.txt"
findstr /v /b /c:"%MOD_INTERNAL_NAME% :" "!MODS_TXT!" > "!_MODS_TMP!"
:: findstr /v returns 1 when it filtered every line out, which is a normal
:: result here (our entry was the only one). Only 2+ is a read failure.
if errorlevel 2 (
    del "!_MODS_TMP!" 2>nul
    echo   ERROR: could not read !MODS_TXT! to deregister %MOD_INTERNAL_NAME%.
    exit /b 1
)
move /y "!_MODS_TMP!" "!MODS_TXT!" >nul
if errorlevel 1 (
    del "!_MODS_TMP!" 2>nul
    echo   ERROR: could not rewrite !MODS_TXT!.
    exit /b 1
)
echo   Deregistered %MOD_INTERNAL_NAME% from mods.txt
exit /b 0

:: ============================================
:: Remove the UE4SS loader itself. Only the two files install.cmd's vendored
:: zip lays down as the loader - never Mods\, which holds the user's other
:: Lua mods.
:: ============================================
:remove_UE4SS
:: Every top-level file the vendored UE4SS zip lays down, not just the two
:: DLLs. Removing a subset left the game folder littered with a settings
:: file and docs while reporting "Uninstall Complete", and the bespoke
:: uninstaller this shared path replaces removed four of them.
for %%f in (UE4SS.dll dwmapi.dll UE4SS-settings.ini Changelog.md README.md) do (
    if exist "!UE4_BINARIES_DIR!\%%f" (
        del "!UE4_BINARIES_DIR!\%%f"
        echo   Removed: %%f
    )
)
:: The Mods\ tree is deliberately left alone. It holds UE4SS's own built-in
:: Lua mods AND anything else the user installed, and there is no record of
:: which subfolders arrived with the loader - so deleting the tree would take
:: the user's other mods with it. Our own entry is removed by
:: :remove_ue4ss_mod before this runs.
if exist "!UE4_BINARIES_DIR!\Mods" (
    echo   Left in place: Mods\ ^(UE4SS built-in Lua mods and any you added^)
)
exit /b 0

:: ============================================
:: Shim-only: no framework to remove beyond the shim DLL (handled already).
:: ============================================
:remove_None
exit /b 0
