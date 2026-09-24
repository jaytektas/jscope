; jscope -- Windows installer (Inno Setup 6, free: https://jrsoftware.org/isinfo.php)
;
; Built by packaging/package-windows.sh, which stages the files and passes:
;   iscc /DAppVersion=x.y.z /DStageDir=<staged files> /DOutputDir=<dist> packaging\jscope.iss
;   -> <dist>\jscope-x.y.z-setup.exe
;
; The file name matters: jscope's update check picks the release file ending "-setup.exe" (JFramework's
; JRelease), and the version must be the one in CMakeLists.txt's project(... VERSION).
;
; SMOOTH UPGRADES come from three things here:
;   AppId                -- NEVER change it. It is how a new installer knows it is upgrading this jscope
;                           rather than installing a second one beside it.
;   PrivilegesRequired   -- lowest: installs for the current user (%LOCALAPPDATA%\Programs), so an update
;                           asks for no administrator password.
;   [Run] without skipifsilent -- jscope updates itself by running this installer with /SILENT, and the
;                           last entry starts the new jscope when it finishes.
;
; THE USB DRIVER is the one step that needs an administrator, so it is the one step that asks: it runs
; wdi-simple (third_party/libwdi-win) elevated through "runas". It never runs during a silent install --
; an update must not put a UAC prompt in front of anyone, and the driver is already bound by then.

#ifndef AppVersion
  #error Pass the version: iscc /DAppVersion=x.y.z /DStageDir=... /DOutputDir=... jscope.iss
#endif
#ifndef StageDir
  #error Pass /DStageDir: the folder package-windows.sh staged
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

#define AppName "jscope"

[Setup]
AppId={{FA877A6E-6B27-4632-ACB1-13793A5E94B1}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Jason Roughley
AppPublisherURL=https://github.com/jaytektas/jscope
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
SetupIconFile=jscope.ico
UninstallDisplayIcon={app}\jscope.exe
OutputDir={#OutputDir}
OutputBaseFilename=jscope-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile={#StageDir}\LICENSE.txt

[Tasks]
Name: "usbdriver"; Description: "Install the USB driver for the Hantek 1008C (WinUSB). Hantek's own software will no longer see the scope."; GroupDescription: "USB driver:"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\jscope.exe";            DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\README.txt";            DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "{#StageDir}\LICENSE.txt";           DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\SOURCE.txt";            DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\licences\*";            DestDir: "{app}\licences"; Flags: ignoreversion
Source: "{#StageDir}\driver\wdi-simple.exe"; DestDir: "{app}\driver"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\jscope.exe"
Name: "{autoprograms}\Install the jscope USB driver"; Filename: "{app}\driver\wdi-simple.exe"; \
    Parameters: "--vid 0x0783 --pid 0x5725 --type 0 --name ""Hantek 1008C"" --dest ""{app}\driver\winusb"" --progressbar --timeout 60000"
Name: "{autodesktop}\{#AppName}";  Filename: "{app}\jscope.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\driver\wdi-simple.exe"; \
    Parameters: "--vid 0x0783 --pid 0x5725 --type 0 --name ""Hantek 1008C"" --dest ""{app}\driver\winusb"" --progressbar --timeout 60000"; \
    Verb: runas; Flags: shellexec waituntilterminated; Tasks: usbdriver; Check: not WizardSilent; \
    StatusMsg: "Installing the USB driver for the Hantek 1008C..."
Filename: "{app}\jscope.exe"; Description: "Start {#AppName}"; Flags: nowait postinstall

[UninstallDelete]
Type: filesandordirs; Name: "{app}\driver\winusb"
; The framework's log, written beside the executable while jscope runs.
Type: files; Name: "{app}\genesis.log"
