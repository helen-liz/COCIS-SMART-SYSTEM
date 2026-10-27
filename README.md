# COCIS-SMART-SYSTEM

A system for securing valuable equipment against unauthorized access.

## Smart Control and Monitoring System

Each ESP32 is assigned to a room and to an entry or exit door. It reads four-byte MFRC522 UIDs, checks a persistent administrator-managed allowlist and daily access schedule, actuates a door relay, updates occupancy, controls that room's lighting, logs events, and broadcasts signed occupancy contributions to nearby terminals over ESP-NOW. See [the room map and diagrams](docs/system-diagrams.md).

## Room assignments

The firmware defaults to room ID `1`, named `Lecture Room 1`, with an entry terminal. Assign every terminal to its room by changing `SMART_SYSTEM_ROOM_ID` and `SMART_SYSTEM_ROOM_NAME` in `main/main.c`; set `SMART_SYSTEM_EXIT_NODE` to `1` on that room's exit terminal. Matching room IDs allow entry/exit counts and lighting to be synchronized for that room. Example assignment:

The actual Wokwi circuit is [diagram.json](diagram.json). It shows the hardware for one representative room terminal; use the same wiring at each room and build a separate firmware image with that room's ID/name and entry/exit role. The campus-level logical map and signal flow are in [system-diagrams.md](docs/system-diagrams.md).

| Room ID | Room | Terminals |
| ---: | --- | --- |
| 1 | Lecture Room 1 | Entry and exit |
| 2 | Lecture Room 2 | Entry and exit |
| 3 | Lecture Room 3 | Entry and exit |
| 0 | Main Entrance | Entry and exit, if separately monitored |

These are example labels because the assignment does not provide a floor plan or official room names. Replace them with the actual CCIS room names and install one entry and one exit terminal at each room to track room-level occupancy.

## Hardware defaults

Defaults in `main/main.c` target a classic ESP32 DevKit and MFRC522 SPI module:

| Signal | GPIO |
| --- | ---: |
| SPI SCK / MOSI / MISO | 18 / 23 / 19 |
| MFRC522 CS / RST | 5 / 22 |
| Door strike relay | 26 |
| Room light relay | 27 |

Change the `SMART_SYSTEM_PIN_*` macros for the board. Relay outputs are active-high; use an appropriately rated, isolated relay driver. Never connect mains loads directly to ESP32 GPIO. The RFID reader and relay hardware need suitable supplies and a common signal ground.

Build one image per terminal role. Set `SMART_SYSTEM_EXIT_NODE` to `1` for an exit reader, or leave it `0` for an entry reader. Terminals contributing to a shared occupancy total must use the same Wi-Fi channel (channel 1 by default) and be within ESP-NOW range. Each terminal's signed occupancy contribution is saved in NVS and broadcast once per second; reports older than 10 seconds are omitted. Provide backup power and test radio coverage across the building.

The partition table reserves 1.5 MiB for the application and 2.4 MiB for SPIFFS logs on a 4 MiB flash device. Adjust it for different flash sizes. SPIFFS is suitable for the prototype; an SD-card module is preferable for removable, higher-capacity records.

## First use

1. Connect the board and MFRC522, select the `esp32` target, then build and flash with the ESP-IDF extension.
2. Open the serial monitor at the configured project console baud rate. The default administrator PIN is `1234`; change it immediately with `login 1234`, then `pin <new-4-to-8-digit-PIN>`.
3. Set the device clock with `time YYYY-MM-DD HH:MM:SS`. The firmware denies card access when its clock has not been set. Schedule times use the ESP-IDF local timezone (UTC unless configured otherwise).
4. Present each card and add its UID from the serial console using `login <PIN>` followed by `allow <8-hex-digit-UID>`. The UID is printed in the log after a denied scan.
5. Configure a daily same-day schedule, for example `schedule 07:00 22:00`, then use `status` and `logs` to inspect operation.

## Serial commands

- `help`, `status`, `logs`: show commands, occupancy/schedule/clock, and event history.
- `login <PIN>`, `logout`: authenticate or end the administrator session.
- `allow <UID>`, `deny <UID>`: change the persistent four-byte UID allowlist (maximum 32 cards).
- `schedule <HH:MM> <HH:MM>`: set a daily access window; overnight ranges are not supported.
- `time <YYYY-MM-DD> <HH:MM:SS>`: set the system clock. This is volatile and must be reset after reboot unless a time source is added.
- `pin <new-PIN>`: change the administrator PIN after login.

## Assignment scope and deployment notes

The serial console is the authenticated management interface. Access events go to serial output and `/spiffs/events.csv`; the allowlist, schedule, PIN, and local occupancy contribution are stored in NVS. A device without a set clock fails closed. Correct occupancy and lighting depend on entry and exit terminals reporting consistently; verify totals against manual counts before relying on them operationally.

This is an educational prototype, not a production access-control system. The default PIN must be changed, RFID UIDs can be cloned, ESP-NOW broadcasts are not encrypted/authenticated, and a UART administrator console requires physical access. Production deployment needs secure credentialing, protected admin access, encrypted/authenticated node synchronization, a maintained clock source, tamper-resistant hardware, and safety-reviewed door hardware. Provision nodes with compatible firmware and channel settings.
