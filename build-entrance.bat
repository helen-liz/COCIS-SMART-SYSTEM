@echo off
setlocal

if not defined IDF_PATH set "IDF_PATH=%USERPROFILE%\esp\esp-idf"
if not exist "%IDF_PATH%\export.bat" (
    echo ESP-IDF was not found at "%IDF_PATH%".
    exit /b 1
)

call "%IDF_PATH%\export.bat"
if errorlevel 1 exit /b %errorlevel%

if not exist build-entrance mkdir build-entrance
copy /Y sdkconfig.defaults build-entrance\sdkconfig.entrance >nul
>>build-entrance\sdkconfig.entrance echo # CONFIG_SMART_SYSTEM_ROLE_ROOM1 is not set
>>build-entrance\sdkconfig.entrance echo # CONFIG_SMART_SYSTEM_ROLE_ROOM2 is not set
>>build-entrance\sdkconfig.entrance echo CONFIG_SMART_SYSTEM_ROLE_ENTRANCE=y

idf.py -B build-entrance -D SDKCONFIG="C:/Users/L10/COCIS-SMART-SYSTEM/build-entrance/sdkconfig.entrance" -D SDKCONFIG_DEFAULTS="" build
if errorlevel 1 exit /b %errorlevel%

idf.py -B build-entrance merge-bin
if errorlevel 1 exit /b %errorlevel%

copy /Y build-entrance\Smart-System.elf build\entrance.elf >nul
copy /Y build-entrance\Smart-System.bin build\entrance.bin >nul
copy /Y build-entrance\merged-binary.bin build\entrance-wokwi.bin >nul
echo Main entrance firmware built in build\entrance.elf and build\entrance.bin.
