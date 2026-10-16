; MU2Setup.exe: the Windows download (the user, 2026-10-16, after Malwarebytes Browser Guard listed
; mu-classic.com as riskware). A bare exe that moves itself out of Downloads and puts a shortcut on
; the desktop unasked reads as a dropper; Inno Setup's wizard installs the launcher openly instead:
; the player sees where, ticks the desktop shortcut, and finds MU Classic in Apps & features with
; an uninstaller. Per user, no admin: %LOCALAPPDATA%\Programs\MU2, the launcher's own default
; install (src/launcher/engine.cpp defaultInstall), so the game lands beside it.
;
; Built by `./mu dist launcher` (tools/mu/dist.py) with Inno Setup 6 under Wine:
;   ISCC.exe /DVersion=0.1.0 /DLauncher=<MU2Launcher.exe> /O<dir> mu2setup.iss

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Launcher
  #define Launcher "MU2Launcher.exe"
#endif

[Setup]
; Never changes: Windows knows an install by it, and a newer setup over an older one updates it.
AppId={{6075BEDB-51EA-464F-952E-F39953FEDE1F}
AppName=MU Classic
AppVersion={#Version}
AppVerName=MU Classic
AppPublisher=MU Classic
AppPublisherURL=https://mu-classic.com
AppSupportURL=https://mu-classic.com/contact
AppUpdatesURL=https://mu-classic.com
AppCopyright=Copyright (C) 2026 MU Classic. MU Online is a trademark of WebZen Inc.
VersionInfoVersion={#Version}.0
VersionInfoCompany=MU Classic
VersionInfoDescription=MU Classic Setup
VersionInfoProductName=MU Classic
DefaultDirName={localappdata}\Programs\MU2
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; The launcher's own (src/launcher/main.cpp, Local\MU2Launcher): a running one is closed first.
AppMutex=MU2Launcher
CloseApplications=yes
SetupIconFile=..\app\mu2.ico
UninstallDisplayIcon={app}\MU2Launcher.exe
UninstallDisplayName=MU Classic
; The launcher's look (tools/make_setup_art.py): dark, its near-black ground, the Dark Knight and
; the logo beside Welcome and Finish, the logo at the top of the other pages.
WizardStyle=modern dark hidebevels
WizardBackColor=#0D0B0A
WizardImageFile=art\setup\wizard-100.png,art\setup\wizard-125.png,art\setup\wizard-150.png,art\setup\wizard-175.png,art\setup\wizard-200.png,art\setup\wizard-225.png,art\setup\wizard-250.png
WizardSmallImageFile=art\setup\small-100.png,art\setup\small-125.png,art\setup\small-150.png,art\setup\small-175.png,art\setup\small-200.png,art\setup\small-225.png,art\setup\small-250.png
Compression=lzma2/max
SolidCompression=yes
OutputBaseFilename=MU2Setup

[Tasks]
Name: desktopicon; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#Launcher}"; DestDir: "{app}"; DestName: "MU2Launcher.exe"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\MU Classic"; Filename: "{app}\MU2Launcher.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\MU Classic"; Filename: "{app}\MU2Launcher.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\MU2Launcher.exe"; Description: "{cm:LaunchProgram,MU Classic}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; The game the launcher downloaded, wherever the player put it (engine.h uninstall); the sign-in
; and the launcher's own log in the user folder (AppData\Roaming\MU2) stay.
Filename: "{app}\MU2Launcher.exe"; Parameters: "--uninstall"; RunOnceId: "RemoveGame"; Flags: runhidden waituntilterminated

[UninstallDelete]
; The game's logs/ beside it, kept by the launcher's own Uninstall for a reinstall; removed from
; Apps & features, nothing of MU Classic stays in the folder.
Type: filesandordirs; Name: "{app}\logs"
Type: dirifempty; Name: "{app}"
