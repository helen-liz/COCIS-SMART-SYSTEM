@echo off
setlocal

if not defined IDF_PATH set "IDF_PATH=%USERPROFILE%\esp\esp-idf"
if not exist "%IDF_PATH%\export.bat" (
    echo ESP-IDF was not found at "%IDF_PATH%".
    echo Install ESP-IDF or set IDF_PATH to its installation directory.
    exit /b 1
)

call "%IDF_PATH%\export.bat"
if errorlevel 1 exit /b %errorlevel%

idf.py build %*
if errorlevel 1 exit /b %errorlevel%

idf.py merge-bin
if errorlevel 1 exit /b %errorlevel%

copy /Y build\merged-binary.bin build\wokwi-firmware.bin >nul
echo Browser upload firmware created at build\wokwi-firmware.bin.
exit /b 0
