@echo off
setlocal

if not defined IDF_PATH set "IDF_PATH=%USERPROFILE%\esp\esp-idf"
if not exist "%IDF_PATH%\export.bat" (
    echo ESP-IDF was not found at "%IDF_PATH%".
    exit /b 1
)

call "%IDF_PATH%\export.bat"
if errorlevel 1 exit /b %errorlevel%

if not exist build-room2 mkdir build-room2
copy /Y sdkconfig.defaults build-room2\sdkconfig.room2 >nul
>>build-room2\sdkconfig.room2 echo # CONFIG_SMART_SYSTEM_ROLE_ROOM1 is not set
>>build-room2\sdkconfig.room2 echo # CONFIG_SMART_SYSTEM_ROLE_ENTRANCE is not set
>>build-room2\sdkconfig.room2 echo CONFIG_SMART_SYSTEM_ROLE_ROOM2=y

idf.py -B build-room2 -D SDKCONFIG="C:/Users/L10/COCIS-SMART-SYSTEM/build-room2/sdkconfig.room2" -D SDKCONFIG_DEFAULTS="" build
if errorlevel 1 exit /b %errorlevel%

idf.py -B build-room2 merge-bin
if errorlevel 1 exit /b %errorlevel%

copy /Y build-room2\Smart-System.elf build\room2.elf >nul
copy /Y build-room2\Smart-System.bin build\room2.bin >nul
copy /Y build-room2\merged-binary.bin build\room2-wokwi.bin >nul
echo Lecture Room 2 firmware built in build\room2.elf and build\room2.bin.
