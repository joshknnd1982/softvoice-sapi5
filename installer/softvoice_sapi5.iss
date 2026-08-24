; SoftVoice SAPI 5 - Windows installer
;
; Built by tools\build_all.ps1, which passes the paths in as /D defines. To compile it by
; hand:
;
;   ISCC.exe /DStageDir=..\output /DVersion=1.0.0 installer\softvoice_sapi5.iss
;
; Accessibility notes, since this installer is meant to be usable by the people most likely
; to want these voices:
;   * Every page is a standard Inno Setup page built from real Win32 controls, which screen
;     readers read natively. Nothing is owner-drawn and there is no splash screen.
;   * The components tree and the type combo are given window text in InitializeWizard.
;     Inno exposes no accessible-name property for either. Measured with
;     tools\check_installer_a11y.py on Inno 6.7.3: the combo takes its name from the page's
;     own description text, and the components list reports no container name at all -
;     Inno supplies its own IAccessible for that control and ignores the window text. What
;     matters is that its items are named and carry the check box role, which they do, so a
;     screen reader announces each component as it is focused. The window text is set
;     anyway: it costs nothing and any tool that does read it gets something sensible.
;   * Nothing steals focus, and no page auto-advances.
;   * Every outcome that matters - which interfaces registered, how many voices are visible,
;     where the logs are - is stated in text on the final page and written to the log,
;     rather than being signalled by a colour or an icon.
;   * SetupLogging is on, so a failed install always leaves a full log behind.
;
; Compile with /DProbe to build the same wizard with no payload and no elevation, which is
; how the pages are checked against a screen reader without installing anything.

#ifndef StageDir
  #define StageDir "..\output"
#endif
#ifndef Version
  #define Version "1.0.0"
#endif

#define AppName        "SoftVoice SAPI 5"
#define AppPublisher   "SoftVoice SAPI 5 project"
#define EngineDllName  "SoftVoiceSAPI.dll"
#define ConfigExeName  "SoftVoiceConfig.exe"
#define HostExeName    "svwebspeak-host.exe"

[Setup]
#ifdef Probe
; A separate identity, so an accessibility probe run can never be mistaken for, or clash
; with, a real installation.
AppId={{2F1C9A47-6B3E-4D85-91C0-7A5E28D4B613}
#else
AppId={{8D3B5E20-C471-4A96-BE58-14F7A0C2D9E5}
#endif
AppName={#AppName}
AppVersion={#Version}
AppVerName={#AppName} {#Version}
AppPublisher={#AppPublisher}
AppComments=The 1997 SoftVoice synthesiser as forty SAPI 5 voices - twenty personalities in English and Spanish.
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#ConfigExeName}
DefaultDirName={autopf}\SoftVoice SAPI5
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir={#StageDir}\..\dist
#ifdef Probe
OutputBaseFilename=SoftVoiceSAPI5_AccessibilityProbe
#else
OutputBaseFilename=SoftVoiceSAPI5_Setup_{#Version}
#endif
SetupIconFile={#StageDir}\..\installer\softvoice.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; Both the CLSID registrations and the speech TokenEnums key live under HKLM, which is the
; only place SAPI reads a voice enumerator from.
#ifdef Probe
PrivilegesRequired=lowest
#else
PrivilegesRequired=admin
#endif

; The engine is 32-bit and runs out of process, so it works on 32-bit Windows too; only the
; second interface needs 64-bit Windows. Installing in 64-bit mode is what makes {sys} mean
; the 64-bit System32 and {syswow64} the 32-bit one, which the registration below depends
; on. On 32-bit Windows both constants resolve to the same folder and the code still holds.
ArchitecturesInstallIn64BitMode=x64compatible

; A full log is written to the temp folder and copied beside the program at the end.
SetupLogging=yes

AlwaysShowComponentsList=yes
ShowComponentSizes=yes
InfoBeforeFile={#StageDir}\before_install.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";   Description: "Everything - all forty voices, both interfaces, the settings utility and the tools"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "engine"; Description: "Speech engine, all forty voices, both SAPI 5 interfaces and the settings utility (required)"; Types: full custom; Flags: fixed
Name: "tools";  Description: "Diagnostic tools (sample renderer, SAPI test harness, self-test and latency meter)"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut to the SoftVoice settings"; GroupDescription: "Shortcuts:"

[Files]
#ifdef Probe
Source: "{#StageDir}\before_install.txt"; DestDir: "{app}"; Components: engine
#else
; ---- the SAPI 5 interfaces ---------------------------------------------------------------
; The 32-bit DLL serves 32-bit applications and the 64-bit DLL serves 64-bit ones. Both are
; built from identical sources and both drive the same 32-bit helper, so unlike most
; dual-architecture speech engines there is no surrogate and no second engine anywhere in
; this package.
Source: "{#StageDir}\{#EngineDllName}";     DestDir: "{app}";     Components: engine; Flags: ignoreversion
Source: "{#StageDir}\x64\{#EngineDllName}"; DestDir: "{app}\x64"; Components: engine; Flags: ignoreversion; Check: IsWin64

; ---- the engine and its host --------------------------------------------------------------
; All four are required. Svspan32.dll is what makes the engine load Spanish; without it the
; twenty Spanish voices are not published at all.
Source: "{#StageDir}\{#HostExeName}"; DestDir: "{app}"; Components: engine; Flags: ignoreversion
Source: "{#StageDir}\SVctl32.DLL";    DestDir: "{app}"; Components: engine; Flags: ignoreversion
Source: "{#StageDir}\SVENG32.DLL";    DestDir: "{app}"; Components: engine; Flags: ignoreversion
Source: "{#StageDir}\Svspan32.dll";   DestDir: "{app}"; Components: engine; Flags: ignoreversion

; ---- the configuration utility -------------------------------------------------------------
Source: "{#StageDir}\{#ConfigExeName}"; DestDir: "{app}"; Components: engine; Flags: ignoreversion

; ---- supporting files ----------------------------------------------------------------------
Source: "{#StageDir}\open_logs.cmd";       DestDir: "{app}"; Components: engine; Flags: ignoreversion
Source: "{#StageDir}\before_install.txt";  DestDir: "{app}"; DestName: "readme.txt"; Components: engine; Flags: ignoreversion

; ---- diagnostics -----------------------------------------------------------------------------
Source: "{#StageDir}\sv_render.exe";       DestDir: "{app}";     Components: tools; Flags: ignoreversion
Source: "{#StageDir}\sv_speak.exe";        DestDir: "{app}";     Components: tools; Flags: ignoreversion
Source: "{#StageDir}\sv_selftest.exe";     DestDir: "{app}";     Components: tools; Flags: ignoreversion
Source: "{#StageDir}\sv_latency.exe";      DestDir: "{app}";     Components: tools; Flags: ignoreversion
Source: "{#StageDir}\x64\sv_render.exe";   DestDir: "{app}\x64"; Components: tools; Flags: ignoreversion; Check: IsWin64
Source: "{#StageDir}\x64\sv_speak.exe";    DestDir: "{app}\x64"; Components: tools; Flags: ignoreversion; Check: IsWin64
Source: "{#StageDir}\x64\sv_selftest.exe"; DestDir: "{app}\x64"; Components: tools; Flags: ignoreversion; Check: IsWin64
Source: "{#StageDir}\x64\sv_latency.exe";  DestDir: "{app}\x64"; Components: tools; Flags: ignoreversion; Check: IsWin64
#endif

[Icons]
Name: "{group}\SoftVoice Speech Settings"; Filename: "{app}\{#ConfigExeName}"; Comment: "Adjust the rate, pitch, volume and voice character of the SoftVoice voices"
Name: "{commondesktop}\SoftVoice Speech Settings"; Filename: "{app}\{#ConfigExeName}"; Tasks: desktopicon; Comment: "Adjust the rate, pitch, volume and voice character of the SoftVoice voices"
Name: "{group}\List the installed voices"; Filename: "{app}\sv_speak.exe"; Parameters: "--list"; Components: tools; Comment: "Lists every SAPI 5 voice Windows can see"
Name: "{group}\Open the log folder"; Filename: "{app}\open_logs.cmd"; Comment: "Opens the folder the speech engine writes its logs to"
Name: "{group}\Read me"; Filename: "{app}\readme.txt"; Comment: "What was installed and where"

[Run]
Filename: "{app}\{#ConfigExeName}"; Description: "Open the SoftVoice speech settings"; Flags: postinstall nowait skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{app}\x64"
Type: files;          Name: "{app}\install.log"

[Code]
{ Used to give the components tree and the type combo an accessible name. Inno exposes no
  property for either, and without one a screen reader announces the control as nothing but
  its type. }
procedure SetWindowTextW(Wnd: HWND; Text: String);
  external 'SetWindowTextW@user32.dll stdcall';

var
  RegisteredX86: Boolean;
  RegisteredX64: Boolean;
  RegistrationNotes: String;

procedure Note(const S: String);
begin
  Log('[softvoice] ' + S);
  if RegistrationNotes <> '' then
    RegistrationNotes := RegistrationNotes + #13#10;
  RegistrationNotes := RegistrationNotes + S;
end;

{ A running engine holds svwebspeak-host.exe and the SoftVoice DLLs open, so it has to go
  before files are replaced or removed. The next application to speak starts a fresh one on
  demand, so nothing is lost by doing this. }
procedure StopEngine;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM {#HostExeName} /F', '',
       SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM {#ConfigExeName} /F', '',
       SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

{ regsvr32 is bitness-specific: the copy in System32 registers 64-bit DLLs and the copy in
  SysWOW64 registers 32-bit ones. Getting these the wrong way round is the classic way to
  end up with a voice that registers without error and is then never enumerated. }
function RunRegsvr(const Regsvr32, DllPath, Args: String): Boolean;
var
  ResultCode: Integer;
begin
  Result := Exec(Regsvr32, Args + ' "' + DllPath + '"', '', SW_HIDE,
                 ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
  Log(Format('[softvoice] %s %s "%s" -> %d', [Regsvr32, Args, DllPath, ResultCode]));
end;

{ The 32-bit and 64-bit registrations write to different views of the registry, so both are
  checked. Either one being present means Windows can enumerate the voices for that bitness
  of application. }
function VoicesRegistered: Boolean;
begin
  Result := RegKeyExists(HKEY_LOCAL_MACHINE,
    'SOFTWARE\Microsoft\Speech\Voices\TokenEnums\SoftVoice');
  if not Result then
    Result := RegKeyExists(HKEY_LOCAL_MACHINE,
      'SOFTWARE\WOW6432Node\Microsoft\Speech\Voices\TokenEnums\SoftVoice');
end;

function EnginePresent: Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\{#HostExeName}')) and
            FileExists(ExpandConstant('{app}\SVctl32.DLL')) and
            FileExists(ExpandConstant('{app}\SVENG32.DLL'));
end;

function SpanishPresent: Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\Svspan32.dll'));
end;

procedure InitializeWizard;
begin
  { Checked with tools\check_installer_a11y.py, which reads every control through MSAA the
    way NVDA does. See the note at the top of this file: Inno's own IAccessible for the
    components list ignores this, but its items are named, which is the part a screen
    reader actually announces. }
  SetWindowTextW(WizardForm.ComponentsList.Handle, 'Components to install');
  SetWindowTextW(WizardForm.TypesCombo.Handle, 'Installation type');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  LogPath: String;
begin
  if CurStep = ssInstall then
  begin
    StopEngine;
  end
  else if CurStep = ssPostInstall then
  begin
    RegistrationNotes := '';

    { The 32-bit interface is registered on every machine; the 64-bit one only where there
      is a 64-bit Windows to register it on. }
    RegisteredX86 := RunRegsvr(ExpandConstant('{syswow64}\regsvr32.exe'),
                               ExpandConstant('{app}\{#EngineDllName}'), '/s');
    if RegisteredX86 then
      Note('The 32-bit SAPI 5 interface was registered.')
    else
      Note('The 32-bit SAPI 5 interface could NOT be registered.');

    if IsWin64 then
    begin
      RegisteredX64 := RunRegsvr(ExpandConstant('{sys}\regsvr32.exe'),
                                 ExpandConstant('{app}\x64\{#EngineDllName}'), '/s');
      if RegisteredX64 then
        Note('The 64-bit SAPI 5 interface was registered.')
      else
        Note('The 64-bit SAPI 5 interface could NOT be registered.');
    end
    else
      Note('This is 32-bit Windows, so only the 32-bit interface was needed.');

    if EnginePresent then
      Note('The SoftVoice engine and its host are installed.')
    else
      Note('Warning: part of the speech engine is missing; the voices will not speak.');

    if SpanishPresent then
      Note('All forty voices are available: twenty personalities in English and the same '
           + 'twenty in Spanish.')
    else
      Note('Warning: Svspan32.dll is missing, so only the twenty English voices are '
           + 'available.');

    if VoicesRegistered then
      Note('Windows speech settings can now see the SoftVoice voices.')
    else
      Note('Warning: the speech voice list was not updated. See the log named below.');

    { Keep the installer's own log with the program, where a bug report can find it. }
    LogPath := ExpandConstant('{log}');
    if LogPath <> '' then
    begin
      CopyFile(LogPath, ExpandConstant('{app}\install.log'), False);
      Note('A full installation log was saved as ' + ExpandConstant('{app}\install.log') + '.');
    end;
    { Written as an unexpanded environment variable on purpose: under an administrative
      install the localappdata constant resolves to the administrator's folder, not the
      folder the person who actually uses the voices will find their logs in. }
    Note('The speech engine writes its own logs to %LOCALAPPDATA%\SoftVoice SAPI5\Logs'
         + ' - there is a shortcut to it in the Start menu.');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    StopEngine;
    RunRegsvr(ExpandConstant('{syswow64}\regsvr32.exe'),
              ExpandConstant('{app}\{#EngineDllName}'), '/s /u');
    if IsWin64 then
      RunRegsvr(ExpandConstant('{sys}\regsvr32.exe'),
                ExpandConstant('{app}\x64\{#EngineDllName}'), '/s /u');
    { Give the loader a moment to let go of the DLLs before the files are deleted. }
    Sleep(1500);
  end;
end;

{ The finish page normally shows one fixed line. Replacing it with the notes above means a
  screen reader reads the actual outcome - which interfaces registered, whether the engine
  is there, where the logs are - instead of a generic success message. }
procedure CurPageChanged(CurPageID: Integer);
var
  Blank: String;
  Summary: String;
begin
  if (CurPageID = wpFinished) and (RegistrationNotes <> '') then
  begin
    Blank := #13#10 + #13#10;
    Summary := '{#AppName} has been installed.' + Blank + RegistrationNotes + Blank +
      'Choose a SoftVoice voice in your screen reader or in Windows speech settings. ' +
      'Rate, pitch, volume and eleven other parameters can be adjusted per voice in the ' +
      'SoftVoice Speech Settings utility, and any change there takes effect on the next ' +
      'thing spoken.';
    WizardForm.FinishedLabel.AutoSize := False;
    WizardForm.FinishedLabel.Height := WizardForm.FinishedLabel.Parent.ClientHeight - 8;
    WizardForm.FinishedLabel.Caption := Summary;
  end;
end;
