; Compile with scripts/package-client.ps1. Keep AppId stable across releases.
#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef StageDir
  #error StageDir is required
#endif
#ifndef OutputPath
  #error OutputPath is required
#endif

[Setup]
AppId={{B4CFF4F6-9EF4-426A-86AA-D46897E47D19}
AppName=NebulaChat
AppVersion={#AppVersion}
AppPublisher=NebulaChat
DefaultDirName={localappdata}\Programs\NebulaChat
DefaultGroupName=NebulaChat
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputPath}
OutputBaseFilename=NebulaChat-{#AppVersion}-windows-x64-setup
UninstallDisplayIcon={app}\chat_client.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
AppMutex=NebulaChatClientRunning
RestartApplications=no
SetupMutex=NebulaChatInstaller
DisableProgramGroupPage=yes
LicenseFile={#StageDir}\licenses\NebulaChat.txt

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "config.ini"; Flags: ignoreversion recursesubdirs createallsubdirs
; Preserve an existing installation's endpoint and local edits on manual upgrades.
Source: "{#StageDir}\config.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{group}\NebulaChat"; Filename: "{app}\chat_client.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\NebulaChat"; Filename: "{app}\chat_client.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\chat_client.exe"; Description: "Launch NebulaChat"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[Code]
function NextVersionPart(var Value: String): Integer;
var
  Separator: Integer;
  Part: String;
begin
  Separator := Pos('.', Value);
  if Separator = 0 then begin
    Part := Value;
    Value := '';
  end else begin
    Part := Copy(Value, 1, Separator - 1);
    Delete(Value, 1, Separator);
  end;
  Result := StrToIntDef(Part, 0);
end;

function IsNewer(Installed, Incoming: String): Boolean;
var
  Index, OldPart, NewPart: Integer;
begin
  Result := False;
  for Index := 1 to 3 do begin
    OldPart := NextVersionPart(Installed);
    NewPart := NextVersionPart(Incoming);
    if OldPart <> NewPart then begin
      Result := OldPart > NewPart;
      Exit;
    end;
  end;
end;

function InitializeSetup(): Boolean;
var
  Installed: String;
begin
  Result := True;
  if RegQueryStringValue(HKCU64,
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\{B4CFF4F6-9EF4-426A-86AA-D46897E47D19}_is1',
    'DisplayVersion', Installed) then begin
    if IsNewer(Installed, '{#AppVersion}') then begin
      SuppressibleMsgBox('A newer version of NebulaChat (' + Installed +
        ') is already installed. Downgrades are not supported.', mbError, MB_OK, IDOK);
      Result := False;
    end;
  end;
end;
