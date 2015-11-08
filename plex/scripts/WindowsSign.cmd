@echo off

rem
rem Usage - Sign.cmd <target file>
rem

setlocal

set TargetFile=%~1

signtool.exe sign /a /sm /t http://timestamp.verisign.com/scripts/timstamp.dll "%TargetFile%"
if "%errorlevel%" == 1 (
  echo FATAL ERROR - could sign %TargetFile%
  exit /b 1
)

signtool.exe verify /pa "%TargetFile%"
if "%errorlevel%" == 1 (
  echo FATAL ERROR - could not verify signature for %TargetFile%
  exit /b 1
)

endlocal
