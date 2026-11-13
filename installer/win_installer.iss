; The Windows installer of AfterNight, compiled by Inno Setup 6.6 or later:
;
;   iscc win_installer.iss /DMSVC                 the installer for one account, which asks for no rights
;   iscc win_installer.iss /DMSVC /DADMIN         the installer for every account
;   ... /DREQUIRE_DOCS                            a release: a missing user guide stops the build
;
; The version is stated once, below, and build/scripts/tools/check_installer_payload.py holds it to the version the
; application states. The wizard's pictures are drawn by build/scripts/tools/render_installer_artwork.py.

#if Ver < EncodeVer(6, 6, 0)
  #error Inno Setup 6.6 or later is needed: the dark wizard and its pictures per display size do not exist before it.
#endif

#define MyAppName "AfterNight"
#define MyAppVersion "1.1.0"
#define MyAppPublisher "Ezequiel Ruiz"
#define MyAppExeName "AfterNight.exe"
#define MyAppId "{9D5A6D59-2A02-4464-A8CA-939FCDA20912}"
#define MyAppUrl "https://github.com/emruiz81/afternight"

[Setup]

AppId={{#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppUrl}
AppSupportURL={#MyAppUrl}/issues
AppUpdatesURL={#MyAppUrl}/releases
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes
; The first page names what is being installed and its version, with the product's panel beside it.
DisableWelcomePage=no
#ifdef ADMIN
PrivilegesRequired=admin
#else
PrivilegesRequired=lowest
#endif
OutputDir=.\

#ifdef ADMIN
OutputBaseFilename=AfterNightSetup_{#MyAppVersion}
#else
OutputBaseFilename=AfterNightUserSetup_{#MyAppVersion}
#endif
Compression=lzma
SolidCompression=yes
; One wizard at a time: two of them removing and writing one installation leave neither whole.
SetupMutex=AfterNightSetup_9D5A6D59
; An earlier installation is removed whole before a file is copied (PrepareToInstall below), and a running
; AfterNight is asked to be closed there, so setup has no file in use to replace and nothing to restart.
CloseApplications=no
; The application's own look: the dark blue of its mark in both Windows modes, title bar included, with no ruled
; lines. The panel of the first and last pages and the mark in the corner of the others each come in the seven sizes
; the wizard shows them at between 100% and 250%; setup takes the one that fits.
WizardStyle=modern dark polar includetitlebar hidebevels
WizardImageFile=artwork\wizard-panel-*.png
WizardImageBackColor=#0A1429
WizardSmallImageFile=artwork\wizard-mark-*.png
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupIconFile=..\icons\afternight_icon_512x512_blue.ico
VersionInfoProductName={#MyAppName}
VersionInfoDescription={#MyAppName} Setup
VersionInfoCopyright="Ezequiel Ruiz <ezequiel.ruiz@gmail.com>"
VersionInfoVersion={#MyAppVersion}
;LicenseFile=.\license.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The deployed directory is the product: the engine and its G'MIC adapter, the private RAW runtime the engine
; imports, AfterNightPlatform (the logger and the crash handler every binary loads), the consumer runtime, the
; libraries, the application, both extension hosts, the command-line tool, the runtime backend, the style plugin and
; the crash handler process. Development builds also leave beside it the engine's build record, the test, fuzz,
; benchmark and RAW qualification tools, the measurement driver, the reference libraries only the library parity
; suite imports, the optional Python adapter, the opt-in GPU backend and the Debug builds of the private RAW runtime;
; build/scripts/tools/check_installer_payload.py proves each is excluded and that the product's own libraries, the
; platform library among them, are not.
Source: "..\bin\msvc\release\*"; DestDir: "{app}"; Excludes: "*.gitkeep,gtest.dll,*.log,*.pdb,.engine-instrumentation,Test_*,Fuzz_*,Raw*Bench*,Raw*Oracle*,RawRuntimeProbe*,RawNativeTests*,AfterNightEngineBench*,AfterNightWorkflowDriver*,AfterNightEnginePythonAdapter*,AfterNightEngineOpenClBackend*,afternight_libraw_rd.*,afternight_rawspeedd.*,opencv_*,raw_r*,lcms2-2*"; Flags: ignoreversion recursesubdirs

; Documentation (release builds require the pre-built HTML artifact; local builds include it when present)
#ifdef REQUIRE_DOCS
Source: "..\docs\user-guide\site\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs
#else
Source: "..\docs\user-guide\site\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
#endif

[Dirs]
#ifdef ADMIN
; Grants full control (read/write/execute) to all users for the installation folder
Name: "{app}"; Permissions: everyone-full
#endif

[UninstallDelete]
; The application writes beside itself - the bundled Python compiles its modules there - so removing only what setup
; copied would leave the folder behind. The wizard therefore refuses a folder that already holds something else
; (NextButtonClick below).
Type: filesandordirs; Name: "{app}"

[Registry]
; HKA is the machine's key in the installer for every account and the account's own in the other.
Root: HKA; Subkey: "Software\{#MyAppName}"; ValueType: string; ValueName: "Version"; ValueData: "{#MyAppVersion}"; Flags: uninsdeletekey

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autoprograms}\{#MyAppName} User Guide"; Filename: "{app}\docs\index.html"; Flags: createonlyiffileexists
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
// An installation is replaced, never written over: a library or a Python module an earlier version shipped and this
// one does not would otherwise stay beside the application, which loads what it finds there. So setup removes the
// installation it finds with that installation's own uninstaller, without a window, once the user has chosen
// Install - not before the wizard opens, where cancelling the wizard would leave nothing installed.

const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppId}_is1';
  ProductKey = 'Software\{#MyAppName}';
  GenericWrite = $40000000;
  OpenExisting = 3;
  InvalidHandle = -1;
  ErrorSharingViolation = 32;
  ErrorUserMappedFile = 1224;

function CreateFile(FileName: String; DesiredAccess, ShareMode, SecurityAttributes, CreationDisposition,
  FlagsAndAttributes, TemplateFile: Cardinal): Longint;
  external 'CreateFileW@kernel32.dll stdcall';
function CloseHandle(Handle: Longint): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';

// The version of the installation this setup would replace, as that installation registered it.
function InstalledVersion(var Version: String): Boolean;
begin
  Result := RegQueryStringValue(HKA, UninstallKey, 'DisplayVersion', Version)
    or RegQueryStringValue(HKA, ProductKey, 'Version', Version);
end;

// The folder of that installation, without a trailing backslash, or '' when none is registered.
function InstalledFolder(): String;
begin
  if not RegQueryStringValue(HKA, UninstallKey, 'Inno Setup: App Path', Result) then
    Result := '';
  Result := RemoveBackslashUnlessRoot(Result);
end;

// Versions compare by their numbers, so 1.10.0 follows 1.9.0. A version that is no row of numbers compares as
// earlier than one that is, and two of them as equal.
function CompareVersions(const Left, Right: String): Integer;
var
  LeftPacked, RightPacked: Int64;
  LeftRead, RightRead: Boolean;
begin
  LeftRead := StrToVersion(Left, LeftPacked);
  RightRead := StrToVersion(Right, RightPacked);
  if LeftRead and RightRead then
    Result := ComparePackedVersion(LeftPacked, RightPacked)
  else if LeftRead then
    Result := 1
  else if RightRead then
    Result := -1
  else
    Result := 0;
end;

// The programs of a folder that are running, by name, separated by commas. Windows refuses to open a running
// program's file for writing, with one of two errors that mean nothing else, and that is the test: it needs nothing
// from the program, so it also sees a version that was installed before this setup existed. The uninstaller is left
// out: while it works, the copy in the folder is itself running, waiting for the copy that does the removing.
function RunningPrograms(const Folder: String): String;
var
  Found: TFindRec;
  Handle, Error: Longint;
begin
  Result := '';
  if (Folder = '') or not FindFirst(AddBackslash(Folder) + '*.exe', Found) then
    exit;
  try
    repeat
      if (Found.Attributes and FILE_ATTRIBUTE_DIRECTORY = 0) and (Pos('unins', Lowercase(Found.Name)) <> 1) then
      begin
        Handle := CreateFile(AddBackslash(Folder) + Found.Name, GenericWrite, 0, 0, OpenExisting, 0, 0);
        if Handle = InvalidHandle then
        begin
          Error := DLLGetLastError();
          if (Error = ErrorSharingViolation) or (Error = ErrorUserMappedFile) then
          begin
            if Result <> '' then
              Result := Result + ', ';
            Result := Result + Found.Name;
          end;
        end
        else
          CloseHandle(Handle);
      end;
    until not FindNext(Found);
  finally
    FindClose(Found);
  end;
end;

// Asks for the running programs of a folder to be closed until none is, and returns whether none is. With message
// boxes suppressed nobody can close anything, and the answer is the first look's.
function ProgramsAreClosed(const Folder: String): Boolean;
var
  Running: String;
begin
  Result := False;
  repeat
    Running := RunningPrograms(Folder);
    if Running = '' then
    begin
      Result := True;
      exit;
    end;
  until SuppressibleMsgBox('{#MyAppName} is running (' + Running + ').' + #13#10#13#10 + 'Close it, then try again.',
    mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY;
end;

function InitializeSetup(): Boolean;
var
  Installed: String;
begin
  Result := True;
  if InstalledVersion(Installed) and (CompareVersions(Installed, '{#MyAppVersion}') > 0) then
    Result := SuppressibleMsgBox('{#MyAppName} ' + Installed + ' is installed, and it is newer than the version '
      + 'this setup installs, {#MyAppVersion}.' + #13#10#13#10
      + 'Replace it with the older version?', mbConfirmation, MB_YESNO, IDNO) = IDYES;
end;

// Whether a folder holds anything at all.
function FolderHoldsFiles(const Folder: String): Boolean;
var
  Found: TFindRec;
begin
  Result := False;
  if not FindFirst(AddBackslash(Folder) + '*', Found) then
    exit;
  try
    repeat
      if (Found.Name <> '.') and (Found.Name <> '..') then
        Result := True;
    until Result or not FindNext(Found);
  finally
    FindClose(Found);
  end;
end;

// Why a folder cannot take the installation, or '' when it can. Uninstalling removes the installation folder whole,
// so the folder is AfterNight's alone: a new one, an empty one, or one an AfterNight is in.
function FolderProblem(const Folder: String): String;
begin
  Result := '';
  if DirExists(Folder) and FolderHoldsFiles(Folder) and not FileExists(AddBackslash(Folder) + '{#MyAppExeName}')
    and (CompareText(Folder, InstalledFolder()) <> 0) then
    Result := 'The folder ' + Folder + ' already holds other files.' + #13#10#13#10
      + 'Uninstalling {#MyAppName} deletes its folder and everything in it. Choose an empty folder, or a new one.';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Problem: String;
begin
  Result := True;
  if CurPageID <> wpSelectDir then
    exit;
  Problem := FolderProblem(RemoveBackslashUnlessRoot(WizardDirValue()));
  if Problem <> '' then
  begin
    SuppressibleMsgBox(Problem, mbError, MB_OK, IDOK);
    Result := False;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Previous, Target, Uninstaller: String;
  ExitCode: Integer;
begin
  Previous := InstalledFolder();
  Target := RemoveBackslashUnlessRoot(WizardDirValue());
  // Asked again here, before anything is removed: the page that chooses the folder is not shown when an
  // installation exists, and a folder named on the command line never passes through it.
  Result := FolderProblem(Target);
  if Result <> '' then
    exit;
  if not ProgramsAreClosed(Previous) or not ProgramsAreClosed(Target) then
  begin
    Result := '{#MyAppName} is running. Close it and run setup again.';
    exit;
  end;

  if not RegQueryStringValue(HKA, UninstallKey, 'UninstallString', Uninstaller) then
    exit;
  Uninstaller := RemoveQuotes(Uninstaller);
  if not FileExists(Uninstaller) then
  begin
    // The installation was deleted by hand and only its registration is left, which this installation replaces.
    Log('The registered uninstaller is gone, so there is nothing to remove: ' + Uninstaller);
    exit;
  end;
  Log('Removing the installation in ' + Previous);
  if not Exec(Uninstaller, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated,
    ExitCode) then
    Result := 'The installed {#MyAppName} could not be removed: its uninstaller did not start ('
      + SysErrorMessage(ExitCode) + '). Remove it from the installed apps of the Windows settings, then run setup '
      + 'again.'
  else if ExitCode <> 0 then
    Result := 'The installed {#MyAppName} could not be removed: its uninstaller ended with code '
      + IntToStr(ExitCode) + '. Remove it from the installed apps of the Windows settings, then run setup again.';
end;

function InitializeUninstall(): Boolean;
begin
  // A running program keeps its files, and an uninstallation that leaves half of them is worse than none.
  Result := ProgramsAreClosed(ExpandConstant('{app}'));
end;
