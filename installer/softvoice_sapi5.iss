; SoftVoice SAPI 5 - Windows installer
;
; Built by tools\build_all.ps1, which passes the paths in as /D defines. To compile it by
; hand:
;
;   ISCC.exe /DStageDir=..\output /DVersion=1.0.0 installer\softvoice_sapi5.iss
;
; Choosing voices
; ---------------
; The engine has twenty personalities and two languages, and every combination of the two
; is a separate SAPI voice - forty in all. Nothing about a voice costs disk space: the
; catalogue is compiled into the DLL and the enumerator builds tokens from it, so a voice
; is "installed" only in the sense that this installation publishes it.
;
; So the choice is recorded rather than acted on. The components page below lists all forty
; as check boxes under two language groups; what is checked is written to voices.ini in the
; program folder, and the enumerator publishes exactly the voices that file keeps. The one
; thing that is a real file is Spanish itself: Svspan32.dll is the engine's Spanish rule
; set, and it is only laid down when at least one Spanish voice was chosen.
;
; Re-running this installer is the supported way to change the selection afterwards - Inno
; restores the previous set of components, so it opens on what was chosen last time.
;
; Accessibility notes, since this installer is meant to be usable by the people most likely
; to want these voices:
;   * Every page is a standard Inno Setup page built from real Win32 controls, which screen
;     readers read natively. Nothing is owner-drawn and there is no splash screen.
;   * The forty voices are check boxes in the standard components tree rather than a custom
;     page, because that control already reports the check box role and the item name to
;     MSAA, and is already keyboard navigable.
;   * Each voice's description carries the language in the same form the voice itself uses
;     - "Male", "Male (Spanish)" - so an item read on its own is unambiguous, without the
;     listener having to remember which group they are in.
;   * The components tree and the type combo are given window text in InitializeWizard.
;     Inno exposes no accessible-name property for either. Measured with
;     tools\check_installer_a11y.py on Inno 6.7.3: the combo takes its name from the page's
;     own description text, and the components list reports no container name at all -
;     Inno supplies its own IAccessible for that control and ignores the window text. What
;     matters is that its items are named and carry the check box role, which they do, so a
;     screen reader announces each component as it is focused. The window text is set
;     anyway: it costs nothing and any tool that does read it gets something sensible.
;   * Choosing no voices at all is refused with a message box, and focus is put back on the
;     components tree so the correction can be made without hunting for it.
;   * Nothing steals focus, and no page auto-advances.
;   * Every outcome that matters - which interfaces registered, how many voices of each
;     language are published, where the logs are - is stated in text on the final page and
;     written to the log, rather than being signalled by a colour or an icon.
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
#define SelectionFile  "voices.ini"

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
AppComments=The 1997 SoftVoice synthesiser as up to forty SAPI 5 voices - twenty personalities in English and Spanish, chosen one at a time at install time.
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

; Sizes are off because forty of the forty-three components are voices, and a voice takes
; no disk space at all - the catalogue is compiled in. A tree in which almost every line
; reads "0 bytes" tells nobody anything and makes each item longer to listen to. The disk
; space the installation actually needs is still stated on the Ready page.
ShowComponentSizes=no

InfoBeforeFile={#StageDir}\before_install.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
; The stock wording talks about components in the abstract. On this page the components
; are mostly voices, so it says so - this is the first thing read out when the page opens.
WizardSelectComponents=Choose the voices to install
SelectComponentsDesc=Which voices, and which parts of SoftVoice, should be installed?
SelectComponentsLabel2=Pick an installation type from the list, or choose voices one at a time in the tree below. Every voice is a check box, grouped by language: check a language to take all twenty of its voices, or open the group to choose individually. At least one voice is needed. Click Next when you are ready to continue.

[Types]
Name: "full";     Description: "Everything - all forty voices in both languages, plus the diagnostic tools"
Name: "everyday"; Description: "The everyday voices - the eight natural-sounding personalities, in English"
Name: "english";  Description: "English only - all twenty personalities, speaking English"
Name: "spanish";  Description: "Spanish only - all twenty personalities, speaking Spanish"
Name: "custom";   Description: "Custom - choose the languages and the individual voices"; Flags: iscustom

[Components]
Name: "engine"; Description: "Speech engine, both SAPI 5 interfaces and the settings utility (required)"; Types: full everyday english spanish custom; Flags: fixed

; ---- the twenty personalities speaking English ---------------------------------------------
Name: "en";              Description: "English voices - twenty personalities speaking English"; Types: full everyday english
Name: "en\male";         Description: "Male - the engine's own default, a plain adult male"; Types: full everyday english
Name: "en\female";       Description: "Female - a plain adult female"; Types: full everyday english
Name: "en\largemale";    Description: "Large Male - deeper and heavier than Male"; Types: full everyday english
Name: "en\child";        Description: "Child - high and light"; Types: full everyday english
Name: "en\giantmale";    Description: "Giant Male - very deep and very slow"; Types: full english
Name: "en\mellowfemale"; Description: "Mellow Female - softer edged than Female"; Types: full everyday english
Name: "en\mellowmale";   Description: "Mellow Male - softer edged than Male"; Types: full everyday english
Name: "en\crispmale";    Description: "Crisp Male - brighter and more clipped than Male"; Types: full everyday english
Name: "en\thefly";       Description: "The Fly - very high and thin (novelty)"; Types: full english
Name: "en\robotoid";     Description: "Robotoid - flat and mechanical (novelty)"; Types: full english
Name: "en\martian";      Description: "Martian - hollow and otherworldly (novelty)"; Types: full english
Name: "en\colossus";     Description: "Colossus - enormous, deep and slow (novelty)"; Types: full english
Name: "en\fastfred";     Description: "Fast Fred - male, at twice the usual speed"; Types: full english
Name: "en\oldwoman";     Description: "Old Woman - an older female"; Types: full everyday english
Name: "en\munchkin";     Description: "Munchkin - small and squeezed (novelty)"; Types: full english
Name: "en\troll";        Description: "Troll - rough and hurried (novelty)"; Types: full english
Name: "en\nerd";         Description: "Nerd - nasal and precise (novelty)"; Types: full english
Name: "en\milktoast";    Description: "Milktoast - timid and hesitant (novelty)"; Types: full english
Name: "en\tipsy";        Description: "Tipsy - slurred and unsteady (novelty)"; Types: full english
Name: "en\choirboy";     Description: "Choir Boy - high, clear and unhurried"; Types: full english

; ---- the same twenty speaking Spanish -------------------------------------------------------
; The language is repeated in every description on purpose: it is what the voice is called in
; Windows, and it is the only context a screen reader has when the item is read on its own.
Name: "es";              Description: "Spanish voices - the same twenty personalities speaking Spanish"; Types: full spanish
Name: "es\male";         Description: "Male (Spanish) - a plain adult male"; Types: full spanish
Name: "es\female";       Description: "Female (Spanish) - a plain adult female"; Types: full spanish
Name: "es\largemale";    Description: "Large Male (Spanish) - deeper and heavier than Male"; Types: full spanish
Name: "es\child";        Description: "Child (Spanish) - high and light"; Types: full spanish
Name: "es\giantmale";    Description: "Giant Male (Spanish) - very deep and very slow"; Types: full spanish
Name: "es\mellowfemale"; Description: "Mellow Female (Spanish) - softer edged than Female"; Types: full spanish
Name: "es\mellowmale";   Description: "Mellow Male (Spanish) - softer edged than Male"; Types: full spanish
Name: "es\crispmale";    Description: "Crisp Male (Spanish) - brighter and more clipped than Male"; Types: full spanish
Name: "es\thefly";       Description: "The Fly (Spanish) - very high and thin (novelty)"; Types: full spanish
Name: "es\robotoid";     Description: "Robotoid (Spanish) - flat and mechanical (novelty)"; Types: full spanish
Name: "es\martian";      Description: "Martian (Spanish) - hollow and otherworldly (novelty)"; Types: full spanish
Name: "es\colossus";     Description: "Colossus (Spanish) - enormous, deep and slow (novelty)"; Types: full spanish
Name: "es\fastfred";     Description: "Fast Fred (Spanish) - male, at twice the usual speed"; Types: full spanish
Name: "es\oldwoman";     Description: "Old Woman (Spanish) - an older female"; Types: full spanish
Name: "es\munchkin";     Description: "Munchkin (Spanish) - small and squeezed (novelty)"; Types: full spanish
Name: "es\troll";        Description: "Troll (Spanish) - rough and hurried (novelty)"; Types: full spanish
Name: "es\nerd";         Description: "Nerd (Spanish) - nasal and precise (novelty)"; Types: full spanish
Name: "es\milktoast";    Description: "Milktoast (Spanish) - timid and hesitant (novelty)"; Types: full spanish
Name: "es\tipsy";        Description: "Tipsy (Spanish) - slurred and unsteady (novelty)"; Types: full spanish
Name: "es\choirboy";     Description: "Choir Boy (Spanish) - high, clear and unhurried"; Types: full spanish

Name: "tools"; Description: "Diagnostic tools (sample renderer, SAPI test harness, self-test and latency meter)"; Types: full

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
Source: "{#StageDir}\{#HostExeName}"; DestDir: "{app}"; Components: engine; Flags: ignoreversion
Source: "{#StageDir}\SVctl32.DLL";    DestDir: "{app}"; Components: engine; Flags: ignoreversion
Source: "{#StageDir}\SVENG32.DLL";    DestDir: "{app}"; Components: engine; Flags: ignoreversion

; The engine's Spanish letter-to-sound rules, and the only part of the choice that is an
; actual file. Without it the engine cannot load Spanish at all, so it goes down whenever
; any Spanish voice was chosen and is left out - and deleted, if an earlier installation put
; it there - when none was. The check asks the twenty Spanish components one at a time
; rather than asking about their parent, because a parent whose children are only partly
; checked is a state worth being explicit about.
Source: "{#StageDir}\Svspan32.dll";   DestDir: "{app}"; Components: engine; Flags: ignoreversion; Check: SpanishWanted

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
; Written by the code below rather than by a [Files] entry, so uninstall is not told about
; it automatically.
Type: files;          Name: "{app}\{#SelectionFile}"

[Code]
{ Used to give the components tree and the type combo an accessible name. Inno exposes no
  property for either, and without one a screen reader announces the control as nothing but
  its type. }
procedure SetWindowTextW(Wnd: HWND; Text: String);
  external 'SetWindowTextW@user32.dll stdcall';

const
  PersonalityCount = 20;
  LanguageCount = 2;

var
  RegisteredX86: Boolean;
  RegisteredX64: Boolean;
  RegistrationNotes: String;

  { The voice catalogue, in the same order as kPersonalities in src\common\sv_voices.hpp.
    Two names per personality: the component name used in the tree above, and the name the
    engine and voices.ini know it by. Keeping both here is what lets a check box be turned
    into a line of the selection file without either side having to guess. }
  PersonalityId: array[0..PersonalityCount - 1] of String;
  PersonalityName: array[0..PersonalityCount - 1] of String;
  LanguageId: array[0..LanguageCount - 1] of String;
  LanguageName: array[0..LanguageCount - 1] of String;

procedure Note(const S: String);
begin
  Log('[softvoice] ' + S);
  if RegistrationNotes <> '' then
    RegistrationNotes := RegistrationNotes + #13#10;
  RegistrationNotes := RegistrationNotes + S;
end;

procedure InitCatalogue;
begin
  LanguageId[0] := 'en';  LanguageName[0] := 'English';
  LanguageId[1] := 'es';  LanguageName[1] := 'Spanish';

  PersonalityId[0]  := 'male';         PersonalityName[0]  := 'Male';
  PersonalityId[1]  := 'female';       PersonalityName[1]  := 'Female';
  PersonalityId[2]  := 'largemale';    PersonalityName[2]  := 'Large Male';
  PersonalityId[3]  := 'child';        PersonalityName[3]  := 'Child';
  PersonalityId[4]  := 'giantmale';    PersonalityName[4]  := 'Giant Male';
  PersonalityId[5]  := 'mellowfemale'; PersonalityName[5]  := 'Mellow Female';
  PersonalityId[6]  := 'mellowmale';   PersonalityName[6]  := 'Mellow Male';
  PersonalityId[7]  := 'crispmale';    PersonalityName[7]  := 'Crisp Male';
  PersonalityId[8]  := 'thefly';       PersonalityName[8]  := 'The Fly';
  PersonalityId[9]  := 'robotoid';     PersonalityName[9]  := 'Robotoid';
  PersonalityId[10] := 'martian';      PersonalityName[10] := 'Martian';
  PersonalityId[11] := 'colossus';     PersonalityName[11] := 'Colossus';
  PersonalityId[12] := 'fastfred';     PersonalityName[12] := 'Fast Fred';
  PersonalityId[13] := 'oldwoman';     PersonalityName[13] := 'Old Woman';
  PersonalityId[14] := 'munchkin';     PersonalityName[14] := 'Munchkin';
  PersonalityId[15] := 'troll';        PersonalityName[15] := 'Troll';
  PersonalityId[16] := 'nerd';         PersonalityName[16] := 'Nerd';
  PersonalityId[17] := 'milktoast';    PersonalityName[17] := 'Milktoast';
  PersonalityId[18] := 'tipsy';        PersonalityName[18] := 'Tipsy';
  PersonalityId[19] := 'choirboy';     PersonalityName[19] := 'Choir Boy';
end;

{ Whether one voice's check box is ticked. Asked of the leaf component, never of the
  language group: a group with some of its children checked is itself "selected", which is
  the right answer for the group and the wrong one for every voice under it. }
function VoiceSelected(Language, Personality: Integer): Boolean;
begin
  Result := WizardIsComponentSelected(
    LanguageId[Language] + '\' + PersonalityId[Personality]);
end;

function SelectedVoiceCount(Language: Integer): Integer;
var
  P: Integer;
begin
  Result := 0;
  for P := 0 to PersonalityCount - 1 do
    if VoiceSelected(Language, P) then
      Result := Result + 1;
end;

function TotalSelectedVoices: Integer;
var
  L: Integer;
begin
  Result := 0;
  for L := 0 to LanguageCount - 1 do
    Result := Result + SelectedVoiceCount(L);
end;

{ Called from the Svspan32.dll entry in [Files]. }
function SpanishWanted: Boolean;
begin
  Result := SelectedVoiceCount(1) > 0;
end;

{ "no English voices" / "1 English voice" / "12 English voices". Written out rather than
  formatted as a bare number so the finish page reads as a sentence when it is spoken. }
function DescribeCount(Count: Integer; const Language: String): String;
begin
  if Count = 0 then
    Result := 'no ' + Language + ' voices'
  else if Count = 1 then
    Result := '1 ' + Language + ' voice'
  else
    Result := IntToStr(Count) + ' ' + Language + ' voices';
end;

{ The choice itself, written where the enumerator looks for it. One line per voice, all
  forty of them, so the file also serves as the list of what could have been chosen. }
procedure WriteVoiceSelection;
var
  Path, Text, Value: String;
  L, P: Integer;
begin
  Path := ExpandConstant('{app}\{#SelectionFile}');

  Text :=
    '; SoftVoice SAPI 5 - the voices this installation publishes.' + #13#10 +
    ';' + #13#10 +
    '; Written from the choices made on the installer''s components page. One line per' + #13#10 +
    '; voice: 1 publishes it, 0 hides it. Changing a line by hand is supported and takes' + #13#10 +
    '; effect the next time an application asks Windows for its list of voices - nothing' + #13#10 +
    '; needs restarting - but this folder is protected, so an editor started without' + #13#10 +
    '; administrator rights will not be able to save. Re-running the installer is the' + #13#10 +
    '; easier way round: it opens on whatever was chosen last time.' + #13#10 +
    ';' + #13#10 +
    '; Deleting this file publishes every voice the engine can speak, and so does a file' + #13#10 +
    '; in which every line says 0 - an installation with no voices in it would be silent' + #13#10 +
    '; with nothing left to explain why, so it is treated as a mistake rather than obeyed.' + #13#10 +
    ';' + #13#10 +
    '; The Spanish voices need Svspan32.dll in this folder as well. The installer only' + #13#10 +
    '; puts it here when at least one Spanish voice was chosen, so switching a Spanish' + #13#10 +
    '; line on by hand does nothing on its own.' + #13#10 + #13#10 +
    '[Voices]' + #13#10;

  for L := 0 to LanguageCount - 1 do
  begin
    Text := Text + '; ' + LanguageName[L] + #13#10;
    for P := 0 to PersonalityCount - 1 do
    begin
      if VoiceSelected(L, P) then
        Value := '1'
      else
        Value := '0';
      Text := Text + LanguageId[L] + '.' + PersonalityName[P] + '=' + Value + #13#10;
    end;
  end;

  if SaveStringToFile(Path, Text, False) then
    Note('Voices published: ' + DescribeCount(SelectedVoiceCount(0), 'English') +
         ' and ' + DescribeCount(SelectedVoiceCount(1), 'Spanish') + ', ' +
         IntToStr(TotalSelectedVoices) + ' of the forty in all.')
  else
    Note('Warning: the voice selection could not be written to ' + Path +
         ', so all forty voices will be published.');
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

{ An installation that kept no Spanish voices should not keep the Spanish rule set either.
  It only ever gets here when an earlier installation put one down: a first install with no
  Spanish selected never copies the file in the first place. }
procedure RemoveUnwantedSpanish;
var
  Path: String;
begin
  Path := ExpandConstant('{app}\Svspan32.dll');
  if not FileExists(Path) then
    Exit;
  if DeleteFile(Path) then
    Note('No Spanish voices were chosen, so the Spanish rule set left by an earlier ' +
         'installation was removed.')
  else
    Note('No Spanish voices were chosen, but the Spanish rule set could not be removed. ' +
         'The Spanish voices stay hidden regardless.');
end;

function InitializeSetup: Boolean;
begin
  InitCatalogue;
  Result := True;
end;

procedure InitializeWizard;
begin
  { Checked with tools\check_installer_a11y.py, which reads every control through MSAA the
    way NVDA does. See the note at the top of this file: Inno's own IAccessible for the
    components list ignores this, but its items are named, which is the part a screen
    reader actually announces. }
  SetWindowTextW(WizardForm.ComponentsList.Handle, 'Voices and components to install');
  SetWindowTextW(WizardForm.TypesCombo.Handle, 'Installation type');
end;

{ The one selection the wizard refuses. Everything else on the components page is a matter
  of taste; an installation with no voices in it is one that registers a speech engine
  Windows can find and then has nothing for it to say. }
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectComponents) and (TotalSelectedVoices = 0) then
  begin
    MsgBox('No voices are selected.'#13#10#13#10 +
           'Choose at least one voice under "English voices" or "Spanish voices". ' +
           'Installing none of them would register the speech engine with Windows but ' +
           'leave it with nothing to speak.',
           mbError, MB_OK);
    WizardForm.ActiveControl := WizardForm.ComponentsList;
    Result := False;
  end;
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

    { Before anything else, because the rest of this reports on it. }
    WriteVoiceSelection;
    if not SpanishWanted then
      RemoveUnwantedSpanish;

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

    if SpanishWanted then
    begin
      if SpanishPresent then
        Note('The Spanish rule set is installed, so the Spanish voices can speak.')
      else
        Note('Warning: Svspan32.dll is missing, so the Spanish voices will not appear.');
    end;

    if VoicesRegistered then
      Note('Windows speech settings can now see the SoftVoice voices.')
    else
      Note('Warning: the speech voice list was not updated. See the log named below.');

    Note('The chosen voices are listed in ' +
         ExpandConstant('{app}\{#SelectionFile}') + '. Run this installer again at any ' +
         'time to change the selection; it opens on what was chosen this time.');

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
  screen reader reads the actual outcome - which interfaces registered, how many voices of
  each language are published, where the logs are - instead of a generic success message. }
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
