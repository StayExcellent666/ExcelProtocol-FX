; Build through scripts/package-release.ps1. Only plugin-owned files are installed.
#ifndef AppVersion
  #error AppVersion must be supplied
#endif
#ifndef StageDir
  #error StageDir must be supplied
#endif
#ifndef MetadataDir
  #error MetadataDir must be supplied
#endif
#ifndef OutputDir
  #error OutputDir must be supplied
#endif

[Setup]
AppId={{E8C494E6-A33D-4C73-9675-090971B6C2A4}
AppName=ExcelProtocol FX
AppVersion={#AppVersion}
AppPublisher=ExcelProtocol FX contributors
VersionInfoVersion={#VersionNumber}
DefaultDirName={code:FindOBSPath}
UsePreviousAppDir=yes
AppendDefaultDirName=no
DisableDirPage=no
DisableProgramGroupPage=yes
DirExistsWarning=no
UninstallDisplayName=ExcelProtocol FX for OBS Studio
UninstallFilesDir={app}\data\obs-plugins\excelprotocol-fx\uninstall
OutputDir={#OutputDir}
OutputBaseFilename=ExcelProtocol-FX-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
PrivilegesRequired=admin
CloseApplications=no
RestartApplications=no
RestartIfNeededByRun=no
AllowCancelDuringInstall=no
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#MetadataDir}\payload-files.txt"; Flags: dontcopy
Source: "{#StageDir}\obs-plugins\64bit\excelprotocol-fx.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion; BeforeInstall: RequireOBSClosed
Source: "{#StageDir}\data\obs-plugins\excelprotocol-fx\*"; DestDir: "{app}\data\obs-plugins\excelprotocol-fx"; Flags: ignoreversion recursesubdirs createallsubdirs; BeforeInstall: RequireOBSClosed

[Code]
const
  ReparsePoint = $400;
  InvalidAttributes = $FFFFFFFF;
var
  Payload: TArrayOfString;
  Existed: array of Boolean;
  BackupRoot: String;
  InstallationStarted, InstallationCommitted, SnapshotReady: Boolean;

function GetBinaryType(FileName: String; var BinaryType: Cardinal): Boolean;
  external 'GetBinaryTypeW@kernel32.dll stdcall';
function GetFileAttributes(FileName: String): Cardinal;
  external 'GetFileAttributesW@kernel32.dll stdcall';

function AnyOBSRunning: Boolean;
var
  Locator, Service, Processes: Variant;
begin
  { Fail closed if process enumeration is unavailable. Never terminate OBS. }
  Result := True;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name="obs64.exe" OR Name="obs32.exe"');
    Result := Processes.Count > 0;
  except
    Log('Could not confirm OBS is closed. Installation is blocked.');
  end;
end;

procedure RequireOBSClosed;
begin
  if AnyOBSRunning then
    RaiseException('Close all OBS Studio windows and processes before installing ExcelProtocol FX. No processes have been closed by this installer.');
end;

function NoReparsePoints(Path: String): Boolean;
var
  Attributes: Cardinal;
  Parent: String;
begin
  Result := False;
  while Length(Path) > 3 do begin
    Attributes := GetFileAttributes(Path);
    if (Attributes <> InvalidAttributes) and ((Attributes and ReparsePoint) <> 0) then Exit;
    Parent := ExtractFileDir(Path);
    if Parent = Path then Exit;
    Path := Parent;
  end;
  Result := True;
end;

function ValidateOBSRoot(Root: String): String;
var
  BinaryType, VersionMS, VersionLS: Cardinal;
  OBSExe: String;
begin
  Result := 'Select the OBS Studio installation folder containing bin\64bit\obs64.exe.';
  OBSExe := AddBackslash(Root) + 'bin\64bit\obs64.exe';
  if not FileExists(OBSExe) then Exit;
  if not NoReparsePoints(OBSExe) then begin
    Result := 'OBS paths containing junctions or symbolic links are not supported by this installer.';
    Exit;
  end;
  if not GetBinaryType(OBSExe, BinaryType) then Exit;
  if BinaryType <> 6 then begin
    Result := 'ExcelProtocol FX requires Windows x64 OBS Studio.';
    Exit;
  end;
  if not GetVersionNumbers(OBSExe, VersionMS, VersionLS) then Exit;
  if (VersionMS < ((32 shl 16) + 2)) or
     ((VersionMS = ((32 shl 16) + 2)) and (VersionLS < (2 shl 16))) or
     (VersionMS >= (33 shl 16)) then begin
    Result := 'This release supports OBS Studio 32.2.2 through 32.x. Install a compatible OBS version first.';
    Exit;
  end;
  Result := '';
end;

function FindOBSPath(Param: String): String;
var
  Candidate: String;
begin
  Result := ExpandConstant('{autopf}\obs-studio');
  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Candidate) and
     (ValidateOBSRoot(Candidate) = '') then begin
    Result := Candidate;
    Exit;
  end;
  if RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio', 'InstallLocation', Candidate) and
     (ValidateOBSRoot(Candidate) = '') then begin
    Result := Candidate;
    Exit;
  end;
  if RegQueryStringValue(HKCU, 'SOFTWARE\OBS Studio', '', Candidate) and
     (ValidateOBSRoot(Candidate) = '') then begin
    Result := Candidate;
    Exit;
  end;
  if ValidateOBSRoot(Result) = '' then Exit;
  if ValidateOBSRoot('C:\obs-studio') = '' then Result := 'C:\obs-studio';
end;

function ValidPayloadPath(Path: String): Boolean;
begin
  Result := (Pos('..', Path) = 0) and (Pos(':', Path) = 0) and
    (Pos('/', Path) = 0) and (Copy(Path, 1, 1) <> '\') and
    ((Path = 'obs-plugins\64bit\excelprotocol-fx.dll') or
     (Pos('data\obs-plugins\excelprotocol-fx\', Path) = 1));
end;

procedure RemoveEmptyParents(Path, Boundary: String);
begin
  while (Length(Path) >= Length(Boundary)) and (Pos(AddBackslash(Boundary), AddBackslash(Path)) = 1) do begin
    if not RemoveDir(Path) then Exit;
    if Path = Boundary then Exit;
    Path := ExtractFileDir(Path);
  end;
end;

procedure CleanSnapshot;
var
  I: Integer;
  Path: String;
begin
  for I := 0 to GetArrayLength(Payload) - 1 do begin
    Path := AddBackslash(BackupRoot) + Payload[I];
    DeleteFile(Path);
    RemoveEmptyParents(ExtractFileDir(Path), BackupRoot);
  end;
  DeleteFile(AddBackslash(BackupRoot) + 'RECOVERY.txt');
  RemoveDir(BackupRoot);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  I: Integer;
  Target, Backup, Recovery: String;
begin
  Result := ValidateOBSRoot(ExpandConstant('{app}'));
  if Result <> '' then Exit;
  if AnyOBSRunning then begin
    Result := 'Close OBS Studio completely before installing. Setup never closes OBS or installs on restart.';
    Exit;
  end;
  ExtractTemporaryFile('payload-files.txt');
  if not LoadStringsFromFile(ExpandConstant('{tmp}\payload-files.txt'), Payload) then begin
    Result := 'The installation payload list could not be read.';
    Exit;
  end;
  BackupRoot := ExpandConstant('{app}\data\obs-plugins\excelprotocol-fx-recovery');
  if DirExists(BackupRoot) then begin
    Result := 'A previous recovery backup exists at ' + BackupRoot + '. Restore or archive it before retrying.';
    Exit;
  end;
  if not NoReparsePoints(BackupRoot) then begin
    Result := 'Installation paths may not contain junctions or symbolic links.';
    Exit;
  end;
  SetArrayLength(Existed, GetArrayLength(Payload));
  Recovery := 'ExcelProtocol FX installation backup. OBS must be closed during recovery.' + #13#10 +
    'Original root: ' + ExpandConstant('{app}') + #13#10 +
    'Restore listed EXISTED files from their same relative paths in this directory.' + #13#10 +
    'Remove listed NEW files only if rolling back this failed installation.' + #13#10;
  for I := 0 to GetArrayLength(Payload) - 1 do begin
    StringChangeEx(Payload[I], '/', '\', True);
    if not ValidPayloadPath(Payload[I]) then begin
      Result := 'Unsafe path found in installer payload.';
      Exit;
    end;
    Target := ExpandConstant('{app}\') + Payload[I];
    Backup := AddBackslash(BackupRoot) + Payload[I];
    if not NoReparsePoints(Target) then begin
      Result := 'Installation paths may not contain junctions or symbolic links.';
      Exit;
    end;
    Existed[I] := FileExists(Target);
    if Existed[I] then begin
      if not ForceDirectories(ExtractFileDir(Backup)) or not FileCopy(Target, Backup, True) then begin
        Result := 'Could not back up ' + Target + '. Installation has not started.';
        Exit;
      end;
      Recovery := Recovery + 'EXISTED ' + Payload[I] + #13#10;
    end else Recovery := Recovery + 'NEW ' + Payload[I] + #13#10;
  end;
  if not ForceDirectories(BackupRoot) or not SaveStringToFile(AddBackslash(BackupRoot) + 'RECOVERY.txt', Recovery, False) then begin
    Result := 'Could not save the recovery record.';
    Exit;
  end;
  SnapshotReady := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then begin
    RequireOBSClosed;
    InstallationStarted := True;
  end;
  if CurStep = ssPostInstall then begin
    InstallationCommitted := True;
    CleanSnapshot;
  end;
end;

procedure DeinitializeSetup;
var
  I: Integer;
  Target, Backup: String;
  Restored: Boolean;
begin
  if not SnapshotReady or InstallationCommitted then Exit;
  if not InstallationStarted then begin
    CleanSnapshot;
    Exit;
  end;
  Restored := not AnyOBSRunning;
  if Restored then begin
    for I := 0 to GetArrayLength(Payload) - 1 do begin
      Target := ExpandConstant('{app}\') + Payload[I];
      Backup := AddBackslash(BackupRoot) + Payload[I];
      if not NoReparsePoints(Target) then Restored := False
      else if Existed[I] then begin
        if not FileCopy(Backup, Target, False) then Restored := False;
      end else if FileExists(Target) then begin
        if not DeleteFile(Target) then Restored := False;
      end;
    end;
  end;
  if Restored then CleanSnapshot
  else begin
    Log('Recovery backup retained at ' + BackupRoot);
    if not WizardSilent then MsgBox('Installation failed. Close OBS and restore the files listed in ' + BackupRoot + '\RECOVERY.txt before restarting OBS.', mbError, MB_OK);
  end;
end;

function InitializeSetup: Boolean;
begin
  Result := not AnyOBSRunning;
  if not Result and not WizardSilent then
    MsgBox('Close OBS Studio completely before installing ExcelProtocol FX.', mbError, MB_OK);
end;

function InitializeUninstall: Boolean;
begin
  Result := not AnyOBSRunning;
  if not Result and not UninstallSilent then
    MsgBox('Close OBS Studio completely before uninstalling ExcelProtocol FX.', mbError, MB_OK);
end;
