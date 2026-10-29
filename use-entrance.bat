@echo off
echo Switching to Main Entrance firmware...
copy /Y build\entrance.elf build\Smart-System.elf >nul
copy /Y build\entrance.bin build\Smart-System.bin >nul
echo Done. Start Wokwi simulator now (F1 -> Wokwi: Start Simulator)
pause
