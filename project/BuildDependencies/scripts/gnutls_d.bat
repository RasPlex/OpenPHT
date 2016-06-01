@ECHO ON

SET LOC_PATH=%BUILD_DEPS_PATH%\scripts
SET FILES=%LOC_PATH%\gnutls_d.txt

CALL dlextract.bat gnutls %FILES%
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%

cd %TMP_PATH%

copy gnutls-3.4.7-w32\bin\libgcc_s_sjlj-1.dll "%APP_PATH%\system\players\dvdplayer\" /Y
copy gnutls-3.4.7-w32\bin\libgmp-10.dll "%APP_PATH%\system\players\dvdplayer\" /Y
copy gnutls-3.4.7-w32\bin\libgnutls-30.dll "%APP_PATH%\system\players\dvdplayer\" /Y
copy gnutls-3.4.7-w32\bin\libhogweed-4-0.dll "%APP_PATH%\system\players\dvdplayer\" /Y
copy gnutls-3.4.7-w32\bin\libnettle-6-0.dll "%APP_PATH%\system\players\dvdplayer\" /Y
copy gnutls-3.4.7-w32\bin\libp11-kit-0.dll "%APP_PATH%\system\players\dvdplayer\" /Y

cd %LOC_PATH%
