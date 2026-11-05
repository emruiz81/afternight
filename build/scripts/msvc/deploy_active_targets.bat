@echo off
setlocal
cd /d "%~dp0"

REM Deploys a package profile's targets, or the named ones, from bin\msvc\<config> into a clean directory with the
REM libraries their imports reach there, and records every file's hash and imports in deploy-manifest.json. The
REM deployment directory is never the build output directory, so a stale DLL in bin cannot satisfy a deployed
REM binary. Imports are read with llvm-readobj; pass --binary-tool when it is not on PATH. The Linux counterpart is
REM build/scripts/linux/deploy_active_targets.sh.
REM
REM Usage: deploy_active_targets.bat [release^|debug] (--profile ^<name^> ^| --targets ^<a,b^>) [--destination ^<dir^>] [--replace]

where python >NUL 2>&1
if errorlevel 1 (
    echo python was not found on PATH; the deployment is read from the registry by a Python script.
    exit /b 1
)

echo ****************deploy_active_targets****************
python "%~dp0..\tools\active_targets.py" deploy %*
if errorlevel 1 exit /b 1
