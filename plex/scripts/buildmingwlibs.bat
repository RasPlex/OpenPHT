@ECHO OFF
SETLOCAL

rem batch file to compile mingw libs via BuildSetup
SET WORKDIR=%WORKSPACE%
rem set M$ env
call "%VS120COMNTOOLS%vsvars32.bat" || exit /b 1

SET PROMPTLEVEL=noprompt
SET BUILDMODE=clean
FOR %%b in (%1, %2, %3) DO (
  IF %%b==prompt SET PROMPTLEVEL=prompt
  IF %%b==noprompt SET PROMPTLEVEL=noprompt
  IF %%b==clean SET BUILDMODE=clean
  IF %%b==noclean SET BUILDMODE=noclean
)

IF "%WORKDIR%"=="" (
  SET WORKDIR=%~dp0\..\..
)

REM Prepend the msys and mingw paths onto %PATH%
SET MSYS_INSTALL_PATH=%WORKDIR%\project\BuildDependencies\msys
SET PATH=%MSYS_INSTALL_PATH%\mingw\bin;%MSYS_INSTALL_PATH%\bin;%PATH%

SET ERRORFILE=%WORKDIR%\project\Win32BuildSetup\errormingw

SET BS_DIR=%WORKDIR%\project\Win32BuildSetup
rem cd %BS_DIR%

IF EXIST %ERRORFILE% del %ERRORFILE% > NUL

rem compiles a bunch of mingw libs and not more
IF EXIST %WORKDIR%\project\BuildDependencies\msys\bin\sh.exe (
  ECHO starting sh shell
  %WORKDIR%\project\BuildDependencies\msys\bin\sh --login /xbmc/plex/scripts/buildmingwlibs.sh
  GOTO END
) ELSE (
  GOTO ENDWITHERROR
)

:ENDWITHERROR
  ECHO msys environment not found
  ECHO bla>%ERRORFILE%
  EXIT /B 1
  
:END
  ECHO exiting msys environment
  IF EXIST %ERRORFILE% (
    ECHO failed to build mingw libs
    EXIT /B 1
  )
  EXIT /B 0

ENDLOCAL
