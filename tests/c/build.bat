@echo off
rem Builds the three evidence programs against the static quickjs lib.
rem Run tools/build-quickjs.ps1 first.
setlocal
pushd "%~dp0..\.."
set OUT=_build
set QJSLIB=%OUT%\qjs-native\Release\qjs.lib

if not exist "%QJSLIB%" goto :noliber
where cl >nul 2>nul
if errorlevel 1 call :setupvc
where cl >nul 2>nul
if errorlevel 1 goto :nocl
if not exist "%OUT%" mkdir "%OUT%"

cl /nologo /O2 /MT /I native\quickjs tests\c\feature_probe.c /Fo:%OUT%\feature_probe.obj /Fe:%OUT%\feature_probe.exe /link %QJSLIB%
if errorlevel 1 goto :fail
cl /nologo /O2 /MT /EHsc /I native\quickjs /I native\bridge tests\c\bridge_smoke.cpp native\bridge\qjs_bridge.cpp /Fo:%OUT%\ /Fe:%OUT%\bridge_smoke.exe /link %QJSLIB%
if errorlevel 1 goto :fail
cl /nologo /O2 /MT /EHsc /I native\quickjs /I native\bridge tests\c\abi_compare.cpp native\bridge\qjs_bridge.cpp /Fo:%OUT%\ /Fe:%OUT%\abi_compare.exe /link %QJSLIB%
if errorlevel 1 goto :fail
echo built feature_probe / bridge_smoke / abi_compare in %OUT%
popd
exit /b 0

:setupvc
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if exist "%VCVARS%" call "%VCVARS%" >nul
exit /b 0

:fail
echo build failed
popd
exit /b 1

:noliber
echo missing %QJSLIB% - run: pwsh tools/build-quickjs.ps1
popd
exit /b 1

:nocl
echo cl.exe not found - install Visual Studio Build Tools or run from a developer prompt
popd
exit /b 1
