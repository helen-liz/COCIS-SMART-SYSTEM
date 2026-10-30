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

if not exist build-room1 mkdir build-room1
copy /Y sdkconfig.defaults build-room1\sdkconfig.room1 >nul
>>build-room1\sdkconfig.room1 echo # CONFIG_SMART_SYSTEM_ROLE_ENTRANCE is not set
>>build-room1\sdkconfig.room1 echo # CONFIG_SMART_SYSTEM_ROLE_ROOM2 is not set
>>build-room1\sdkconfig.room1 echo CONFIG_SMART_SYSTEM_ROLE_ROOM1=y

idf.py -B build-room1 -D SDKCONFIG="C:/Users/L10/COCIS-SMART-SYSTEM/build-room1/sdkconfig.room1" -D SDKCONFIG_DEFAULTS="" build %*
if errorlevel 1 exit /b %errorlevel%

idf.py -B build-room1 merge-bin
if errorlevel 1 exit /b %errorlevel%

copy /Y build-room1\Smart-System.elf build\room1.elf >nul
copy /Y build-room1\Smart-System.bin build\room1.bin >nul
copy /Y build-room1\merged-binary.bin build\room1-wokwi.bin >nul
copy /Y build-room1\merged-binary.bin build\wokwi-firmware.bin >nul
copy /Y build-room1\flasher_args.json build\flasher_args.json >nul
echo Lecture Room 1 firmware built in build\room1.elf and build\room1.bin.
echo Browser upload firmware created at build\room1-wokwi.bin.
exit /b 0
