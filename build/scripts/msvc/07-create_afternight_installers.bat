@echo off
cd /d "%~dp0"

echo ****************07-create_afternight_installers****************

PUSHD ..\..\..\installer
CALL "C:\Program Files (x86)\Inno Setup 6\ISCC" win_installer.iss /DMSVC
CALL "C:\Program Files (x86)\Inno Setup 6\ISCC" win_installer.iss /DMSVC /DADMIN
POPD

echo Done!
