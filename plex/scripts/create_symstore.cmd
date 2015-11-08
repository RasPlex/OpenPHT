@echo off
@setlocal

set SourceRoot=%1
set SymStoreRoot=symbols
rd /s /q "%SymStoreRoot%"
md "%SymStoreRoot%"

symstore add /s "%SymStoreRoot%" /f "plex\OpenPHT.exe" /t PHT /compress
if errorlevel 1 (
  echo ERROR - Symstore OpenPHT.exe failed
  exit /b 1
)

symstore add /s "%SymStoreRoot%" /f "plex\OpenPHT.pdb" /t PHT /compress
if errorlevel 1 (
  echo ERROR - Symstore OpenPHT.pdb failed
  exit /b 1
)

rd /s /q "%SymStoreRoot%\000Admin"
del /f "%SymStoreRoot%\pingme.txt"
del /f /s /q "%SymStoreRoot%\refs.ptr"

%1/project/Win32BuildSetup/tools/7z/7za.exe a -r -tZIP OpenPHT-%2-symsrv.zip "%SymStoreRoot%"

rem rd /s /x "%SymStoreRoot%"
