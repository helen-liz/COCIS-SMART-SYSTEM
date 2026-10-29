@echo off
echo Switching to Lecture Room 1 firmware...
copy /Y build\room1.elf build\Smart-System.elf >nul
copy /Y build\room1.bin build\Smart-System.bin >nul
echo Done. Start Wokwi simulator now (F1 -> Wokwi: Start Simulator)
pause
