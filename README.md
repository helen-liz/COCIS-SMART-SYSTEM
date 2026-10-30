# CoCIS Smart Control and Monitoring System

An ESP-IDF pilot for Makerere University's College of Computing and Information
Sciences (CoCIS). The system automates RFID access at the main entrance, maintains
building and room occupancy, controls lecture-room lighting, and provides three
management interfaces: a serial console, an SSD1306 OLED dashboard, and an optional
Wi-Fi web dashboard.

The pilot covers:

| Terminal | Room ID | Hardware | Main responsibilities |
| --- | ---: | --- | --- |
| Main entrance | 0 | ESP32, two MFRC522 readers, two door relays, SSD1306 | Entry and exit identification, inside list, occupancy, anti-passback |
| Lecture Room 1 | 1 | ESP32, MFRC522, PIR, light relay, SSD1306 | Access, room occupancy, PIR lighting |
| Lecture Room 2 | 2 | ESP32, MFRC522, PIR, light relay, SSD1306 | Access, room occupancy, PIR lighting |

The Wokwi project contains the complete entrance and room hardware on one board so
that every feature can be inspected. Select the active terminal in `menuconfig` and
rebuild the firmware for each role.

## Quick start

On Windows, use the included `build.bat` launcher. It loads the ESP-IDF
environment before invoking `idf.py`, so a system-wide `idf.py` installation is
not required:

```bat
build.bat
```

If ESP-IDF is installed somewhere other than `%USERPROFILE%\esp\esp-idf`, set
`IDF_PATH` to that directory before running the launcher.

On an ESP-IDF terminal, the equivalent commands are:

```bash
idf.py set-target esp32
idf.py menuconfig     # CoCIS Smart System -> Terminal role
idf.py build
```

To build Lecture Room 2 separately, run:

```bat
build-room2.bat
use-room2.bat
```

Then start Wokwi. The role-specific build is kept in `build-room2/` and the
launcher copies its firmware into the `build/` paths referenced by `wokwi.toml`.

To build and run the main entrance role, run:

```bat
build-entrance.bat
use-entrance.bat
```

This builds the dual-reader entrance firmware in `build-entrance/` and switches
the Wokwi firmware to the entrance image.

### Wokwi browser upload

The Wokwi browser cannot read this local repository automatically. Build the role
you want, then open a Wokwi ESP32 project, open the command palette with `F1`,
choose **Upload Firmware and Start Simulation...**, and select the generated
merged binary:

| Role | Build command | Upload file |
| --- | --- | --- |
| Lecture Room 1 | `build.bat` | `build/room1-wokwi.bin` |
| Lecture Room 2 | `build-room2.bat` | `build/room2-wokwi.bin` |
| Main entrance | `build-entrance.bat` | `build/entrance-wokwi.bin` |

Copy the contents of `diagram.json` into the Wokwi project before starting the
simulation. The serial monitor then shows the selected terminal role.

For Wokwi, open the project folder and start the simulation. `wokwi.toml` points to
`build/flasher_args.json` and `build/Smart-System.elf`.

Default terminal role: **Lecture Room 1**. For the full entrance demonstration,
change the role to **Main entrance (entry + exit RFID sections)** and rebuild.

## First use

1. Open the serial monitor.
2. Log in with the default PIN: `login 1234`.
3. Set the clock: `time 2026-10-29 14:00:00`.
4. Enroll a card: `enroll`, then tap the card on the reader.
5. Check the schedule: `schedule 07:00 22:00`.
6. Use `status`, `who`, `rooms`, `events`, and `stats` to monitor the system.
7. Change the default PIN immediately: `pin 5678`.

The clock uses East Africa Time (UTC+3) and is not battery-backed. A production
deployment should add an RTC or NTP source.

## Interfaces

### OLED dashboard

The 128x64 SSD1306 shows the clock, room/terminal name, occupancy, access state,
light state, and the last access event. Access decisions appear as full-screen
`ACCESS GRANTED` or `ACCESS DENIED` cards.

### Serial console

The serial console is the authenticated management interface. It uses ANSI colour,
boxed tables, and a live status dashboard.

Colour output is disabled by default for compatibility with the Wokwi browser
serial monitor. Use `color on` on terminals that support ANSI escape sequences.

```text
Monitoring: status, who, rooms, events [n], logs, stats, uptime, menu, clear
Admin:      login <PIN>, logout, enroll [seconds], allow <UID>, deny <UID>,
            schedule <HH:MM> <HH:MM>, time <YYYY-MM-DD> <HH:MM:SS>, pin <digits>,
            lockdown on|off, emergency on|off, override <minutes>,
            export, wipe, reboot, color on|off
```

### Web dashboard (optional)

Enable it in `menuconfig` -> `CoCIS Smart System` -> `Enable the Wi-Fi web dashboard`.
The ESP32 creates the access point `CoCIS-Smart-<room>`; browse to
`http://192.168.4.1`. The dashboard is read-only and refreshes automatically.

The web dashboard is disabled by default because the Wokwi simulator cannot reach
the simulated access point. It is intended for real hardware.

## Hardware defaults

| Signal | GPIO |
| --- | ---: |
| SPI SCK / MOSI / MISO | 18 / 23 / 19 |
| Entry MFRC522 CS / RST | 5 / 22 |
| Exit MFRC522 CS / RST | 4 / 13 |
| Entry door relay | 26 |
| Exit door relay | 33 |
| Room light relay | 27 |
| PIR motion sensor | 32 |
| SSD1306 SDA / SCL | 21 / 25 |

Relay outputs are active-high. Use an isolated relay driver and a separate supply
for door strikes and mains lighting. Never connect a door strike or mains voltage
directly to an ESP32 GPIO.

## Storage

| Data | Location |
| --- | --- |
| Allowlist, schedule, PIN, occupancy, inside list | NVS |
| Access events | `/spiffs/events.csv` |
| Web dashboard assets | Embedded flash string |

The partition table reserves 1.5 MiB for the application and 2.4 MiB for SPIFFS on
a 4 MiB flash device. The CSV log can also be exported from the serial console.

## Synchronization and reliability

- A FreeRTOS mutex protects the allowlist, inside list, counters, and security state
  from concurrent console and reader access.
- Each terminal stores its own occupancy contribution in NVS.
- Terminals broadcast an occupancy contribution over ESP-NOW every second.
  Peers aggregate matching room IDs and ignore reports older than 10 seconds.
- Access events are appended to SPIFFS only after the state change, and the inside
  list is persisted after each granted entrance/exit.

## Security features

- 4-8 digit administrator PIN with a login lockout after three failures.
- Anti-passback at the main entrance: a card cannot enter twice or exit without
  being inside.
- Duplicate-scan suppression (3 seconds) at each reader.
- Lockdown mode denies entry while allowing exits.
- Emergency release mode opens access and bypasses the schedule.
- Temporary schedule override for authorised events.
- All administrative changes and access outcomes are logged.

## Documentation

- [System operation report](docs/report.md)
- [Architecture and diagrams](docs/system-diagrams.md)
- [Individual contributions](docs/individual-contributions.md)

## Limitations

This is an educational prototype. RFID UIDs can be cloned, ESP-NOW packets are not
encrypted or authenticated, and the serial console requires physical access. A
production deployment needs secure credentials, authenticated node messaging, a
maintained clock source, tamper-resistant hardware, and safety-reviewed door
hardware.
