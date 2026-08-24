@echo off
rem Open the folder the SoftVoice speech engine writes its logs to.
rem
rem Resolved here rather than baked into the shortcut on purpose: under an
rem administrative install the installer's own idea of LOCALAPPDATA is the
rem administrator's folder, not the folder of the person who actually uses the
rem voices and whose logs are the ones worth reading.
set "SVLOGS=%LOCALAPPDATA%\SoftVoice SAPI5\Logs"
if not exist "%SVLOGS%" mkdir "%SVLOGS%" >nul 2>&1
start "" "%SVLOGS%"
