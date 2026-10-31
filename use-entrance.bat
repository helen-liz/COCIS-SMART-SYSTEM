@echo off
echo Switching to Main Entrance firmware...
if not exist build\entrance.elf (
    echo Entrance firmware is missing. Run build-entrance.bat first.
    exit /b 1
)
copy /Y build\entrance.elf build\Smart-System.elf >nul
copy /Y build\entrance.bin build\Smart-System.bin >nul
copy /Y build\entrance-wokwi.bin build\wokwi-firmware.bin >nul
copy /Y build-entrance\flasher_args.json build\flasher_args.json >nul
echo Done. Start Wokwi simulator now (F1 -^> Wokwi: Start Simulator)
