@echo off
setlocal
rem Espressif's upload output contains Unicode; make redirected logs reliable.
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"
set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
if not exist "%PIO_EXE%" (
    >&2 echo VS Code-managed PlatformIO was not found at "%PIO_EXE%". No additional Core will be installed.
    exit /b 1
)
"%PIO_EXE%" %*
exit /b %ERRORLEVEL%
