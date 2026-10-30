@echo off
echo Switching to Lecture Room 2 firmware...
copy /Y build\room2.elf build\Smart-System.elf >nul
copy /Y build\room2.bin build\Smart-System.bin >nul
copy /Y build\room2-wokwi.bin build\wokwi-firmware.bin >nul
copy /Y build-room2\flasher_args.json build\flasher_args.json >nul
echo Done. Start Wokwi simulator now (F1 -> Wokwi: Start Simulator)
pause
