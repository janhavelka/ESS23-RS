@echo off
setlocal
set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
if not exist "%PIO_EXE%" (
    >&2 echo VS Code-managed PlatformIO was not found at "%PIO_EXE%". No additional Core will be installed.
    exit /b 1
)
"%PIO_EXE%" %*
exit /b %ERRORLEVEL%
