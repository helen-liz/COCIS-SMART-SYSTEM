@echo off
echo Switching to Main Entrance firmware...
if not exist build\entrance.elf (
    echo Entrance firmware is missing. Run build-entrance.bat first.
    exit /b 1
)
copy /Y build\entrance.elf build\Smart-System.elf >nul
copy /Y build\entrance.bin build\Smart-System.bin >nul
echo Done. Start Wokwi simulator now (F1 -^> Wokwi: Start Simulator)
