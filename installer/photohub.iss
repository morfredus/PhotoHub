; PhotoHub installer (Inno Setup 6). Run by scripts\windows\package-win.ps1, which defines:
;   /DAppVersion=<version>  /DAppVersionNum=<a.b.c.0>  /DStageDir=<folder to install>
; ASCII only and no BOM, like every other text file of the repository.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppVersionNum
  #define AppVersionNum "0.0.0.0"
#endif
#ifndef StageDir
  #error StageDir must be defined (see package-win.ps1)
#endif

[Setup]
; Never change this identifier: it is how a new version finds and replaces the installed one.
AppId={{E52F7D18-93A4-4B6C-8D20-A1C4F3B96E07}
AppName=PhotoHub
AppVersion={#AppVersion}
AppVerName=PhotoHub {#AppVersion}
AppPublisher=morfredus
AppPublisherURL=https://morfredus.fr
VersionInfoVersion={#AppVersionNum}
DefaultDirName={autopf}\PhotoHub
DefaultGroupName=PhotoHub
AllowNoIcons=yes
UninstallDisplayIcon={app}\PhotoHub.exe
OutputBaseFilename=PhotoHub-{#AppVersion}-win64-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Per-user install by default (no administrator rights); the dialog still offers all users.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
; Close a running PhotoHub instead of failing on a locked file.
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\PhotoHub"; Filename: "{app}\PhotoHub.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\PhotoHub"; Filename: "{app}\PhotoHub.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Run]
Filename: "{app}\PhotoHub.exe"; Description: "{cm:LaunchProgram,PhotoHub}"; Flags: nowait postinstall skipifsilent

[Code]
// The user's data (settings under HKCU\Software\morfredus\PhotoHub) is kept
// by default; the question defaults to "No".
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent then
    if MsgBox(ExpandConstant('{cm:KeepData}'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
    begin
      RegDeleteKeyIncludingSubkeys(HKCU, 'Software\morfredus\PhotoHub');
    end;
end;

[CustomMessages]
english.KeepData=Also delete your PhotoHub settings?%n%nChoose No to keep them for a later reinstall.
french.KeepData=Supprimer aussi vos reglages PhotoHub ?%n%nChoisir Non pour les conserver en vue d'une reinstallation.
