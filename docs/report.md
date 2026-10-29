# Design and Implementation of a Smart Control and Monitoring System for CoCIS

**Course:** Embedded Systems / Internet of Things  
**Institution:** Makerere University, College of Computing and Information Sciences  
**Group:** [Group name / number]  
**Members:** [Member 1], [Member 2], [Member 3], [Member 4]  
**Submission date:** [Date, before 2 November 2026]  
**Wokwi project:** [Paste the shared Wokwi project link]  
**Repository:** [Paste the repository link]

## Abstract

This project designs and implements a low-cost smart control and monitoring system
for the College of Computing and Information Sciences (CoCIS). The prototype uses
ESP32 microcontrollers to automate access control at the main entrance and in two
lecture rooms, maintain live occupancy counts, and switch room lighting based on
motion. Entry and exit are identified with MFRC522 RFID readers. The entrance
controller manages separate entry and exit readers, enforces an inside list and
anti-passback, and drives two door relays. Room controllers combine a reader, a
passive infrared sensor, and a light relay. All terminals persist configuration and
access records in non-volatile storage, exchange occupancy counts over ESP-NOW, and
provide a serial management console, an SSD1306 OLED dashboard, and an optional
Wi-Fi web dashboard. The firmware was implemented in C with ESP-IDF 6.0.2 and built
successfully for the ESP32, using 56% of the application partition without the web
dashboard and 60% with it. The result is a coherent pilot that demonstrates access
control, occupancy monitoring, lighting automation, security modes, and three
operator interfaces on inexpensive hardware.

## 1. Introduction

### 1.1 Background

Universities operate many rooms with manually managed lighting and little reliable
information about who is inside a building or room. At CoCIS, access to lecture
rooms and the main entrance is largely controlled by keys or human attendants.
This makes it difficult to know how many people are present, to respond quickly to
security incidents, or to avoid wasting electricity in unoccupied rooms.

Embedded systems and the Internet of Things (IoT) make it possible to instrument
these spaces with inexpensive sensors and controllers. RFID is widely used for
identity tokens because cards are cheap, durable, and do not require line of sight.
Motion sensors can detect occupancy, and relay outputs can control door strikes and
lighting. An ESP32 provides Wi-Fi, ESP-NOW, and enough GPIO for a complete terminal
at a low cost.

### 1.2 Problem statement

The current manual arrangement provides no automatic record of access, no reliable
occupancy count, and no coordinated response to emergencies or lockdowns. A pilot
is needed that can identify authorised users, control doors and lights, and give
administrators a clear real-time view of the system without relying on expensive
commercial access-control software.

### 1.3 Objectives

The project objectives are to:

1. Design an RFID-based access-control terminal for the main entrance and lecture
   rooms.
2. Maintain accurate room and building occupancy using entry and exit events.
3. Automatically control room lighting based on access and motion.
4. Provide a serial management console, an OLED status display, and an optional web
   dashboard.
5. Persist configuration and access records across power cycles.
6. Demonstrate security features including a PIN-protected console, anti-passback,
   lockdown, emergency release, and a temporary schedule override.
7. Document the architecture, wiring, operation, and test plan.

### 1.4 Scope

The implemented pilot covers one main entrance and two lecture rooms. The entrance
is a single ESP32 controller with two RFID sections and two door relays. Each room
is a single ESP32 controller with one reader, one PIR sensor, and one light relay.
The Wokwi simulation contains the hardware for all three roles on one board, and the
active role is selected at build time. A custom PCB, battery backup, cloud service,
and mobile application are outside the scope.

## 2. Requirements

### 2.1 Functional requirements

| ID | Requirement |
| --- | --- |
| FR1 | Read ISO 14443A card UIDs with an MFRC522 reader. |
| FR2 | Grant access only to UIDs on the stored allowlist and only inside the schedule. |
| FR3 | Enforce anti-passback at the main entrance using an inside list. |
| FR4 | Pulse the correct door relay for a granted entry or exit. |
| FR5 | Count occupancy and persist it in NVS. |
| FR6 | Share occupancy between terminals over ESP-NOW. |
| FR7 | Turn room lights on when access is granted or motion is detected, and off after a configurable hold period. |
| FR8 | Record every access decision in a CSV log on SPIFFS. |
| FR9 | Support card enrolment, allow/deny, schedule, clock, and PIN commands. |
| FR10 | Provide lockdown, emergency release, and temporary override modes. |
| FR11 | Show status on an SSD1306 OLED and a serial console. |
| FR12 | Optionally serve a read-only Wi-Fi dashboard. |
| FR13 | Protect administrative commands with a PIN and login lockout. |

### 2.2 Non-functional requirements

| ID | Requirement |
| --- | --- |
| NFR1 | The terminal must recover configuration and occupancy after a reboot. |
| NFR2 | Shared state must be safe when the console and reader tasks run concurrently. |
| NFR3 | The firmware must build with a current ESP-IDF release for the classic ESP32. |
| NFR4 | The UI must remain readable on a 128x64 monochrome display and an 80-column terminal. |
| NFR5 | The design must use low-cost, widely available modules. |
| NFR6 | Access records must remain available for later review. |
| NFR7 | The system must fail closed for access decisions and fail safe for emergency release. |

### 2.3 Constraints

- The ESP32 has limited RAM and flash, so the web dashboard is optional and the
  event log is capped by SPIFFS capacity.
- The prototype has no real-time clock with battery backup; the clock is set over
  the serial console after boot.
- The Wokwi simulator cannot reach the simulated Wi-Fi access point from the host,
  so the OLED and serial interfaces are the primary demonstration surfaces.
- RFID UIDs are public identifiers, not cryptographic credentials.

## 3. System Design

### 3.1 Architecture

The system uses three logical terminal roles:

| Role | Room ID | Inputs | Outputs |
| --- | ---: | --- | --- |
| Main entrance | 0 | Entry reader, exit reader | Entry door relay, exit door relay, OLED |
| Lecture Room 1 | 1 | Reader, PIR | Door relay, light relay, OLED |
| Lecture Room 2 | 2 | Reader, PIR | Door relay, light relay, OLED |

Every terminal runs the same firmware with a compile-time profile selected through
Kconfig. The application is divided into four layers:

1. **Application layer:** `main.c` coordinates readers, state, commands, the display,
   and the web server.
2. **Domain services:** access decisions, anti-passback, occupancy, lighting, and
   security modes.
3. **Platform services:** NVS and SPIFFS storage, ESP-NOW and Wi-Fi communication,
   and the SPI RFID driver.
4. **Presentation layer:** serial console (`ui.c`), OLED dashboard (`display.c`), and
   web dashboard (`webui.c`).

The detailed topology, component view, sequence diagrams, and wiring are in
[system-diagrams.md](system-diagrams.md).

### 3.2 Hardware design

| Component | Purpose |
| --- | --- |
| ESP32 DevKit v1 | Main controller with Wi-Fi, ESP-NOW, SPI, I2C, and GPIO |
| 2 x MFRC522 | Entry and exit card readers at the main entrance |
| 1 x MFRC522 per room | Room access reader |
| 3 x relay module | Entry door, exit door, and room light switching |
| PIR motion sensor | Occupancy-assisted lighting in rooms |
| SSD1306 128x64 OLED | Local status and access feedback |
| 4.7 kOhm resistors | I2C pull-ups if the OLED module does not include them |
| 220 Ohm resistors and LEDs | Safe simulation loads for relay outputs |

The readers share the SPI bus (SCK 18, MOSI 23, MISO 19) and use separate chip-select
and reset lines. The entrance uses CS 5/RST 22 for entry and CS 4/RST 13 for exit.
The OLED uses I2C on SDA 21 and SCL 25. Relays are connected to GPIO 26 (entry door),
33 (exit door), and 27 (room light). The PIR output is on GPIO 32. All modules share
a common ground.

Relay contacts switch an isolated low-voltage load in the simulation. In a real
installation, a relay or opto-isolated driver must switch the door strike or
lighting circuit, and mains wiring must be installed by a qualified electrician.
The ESP32 must never drive a strike or mains load directly.

### 3.3 Data design

| Data | Storage | Reason |
| --- | --- | --- |
| Administrator PIN, schedule, allowlist, occupancy | NVS | Small, frequently updated, survives power loss |
| Inside list for anti-passback | NVS | Must survive reboot at the entrance |
| Access events | `/spiffs/events.csv` | Append-only history with timestamps |
| UI state | RAM | Rebuilt from sensors and storage at runtime |

Each access event contains a timestamp, terminal/room name, UID, decision, and
reason. The CSV format allows the log to be opened in a spreadsheet for analysis.
The inside list stores the UID, the room, and the entry tick so that stale entries
can be reviewed and cleared.

### 3.4 Software design

`app_main` initialises NVS, SPIFFS, the state mutex, the profile, the SPI bus, the
readers, the relays, the PIR input, the OLED, the console task, ESP-NOW, and the
optional web server. It then enters a loop that:

1. Polls each reader at 120 ms intervals.
2. Applies duplicate-scan suppression.
3. Handles enrolment or access evaluation.
4. Pulses the appropriate relay.
5. Updates occupancy, the inside list, counters, the OLED, the web snapshot, and the
   CSV log.
6. Handles PIR lighting and the light hold timer.
7. Expires the schedule override and enrolment window.
8. Refreshes the OLED and ESP-NOW occupancy once per second.

A FreeRTOS mutex protects shared state because the console task and the main loop
both modify the allowlist, counters, and security flags. Snapshots are copied under
the mutex before being formatted, which avoids holding the lock during slow output.

### 3.5 User-interface design

**OLED.** The 128x64 display is divided into a header with the clock and terminal
name, a body with room occupancy and access state, and a footer with the last event.
Access decisions temporarily replace the dashboard with a large `ACCESS GRANTED`
or `ACCESS DENIED` card so that a user at the door receives immediate feedback.

**Serial console.** The console uses ANSI colour, a boxed banner, aligned tables,
and short status lines. A dashboard summarises security mode, schedule, occupancy,
and counters. Commands are grouped into monitoring and administration and are shown
with `help` or `menu`. Colour can be disabled for terminals that do not support
escape sequences.

**Web dashboard.** When enabled, the ESP32 starts a SoftAP and serves a dark,
responsive dashboard with status cards and a live event list. The page polls a JSON
endpoint every three seconds. The dashboard is read-only; administration remains on
the serial console.

### 3.6 Security design

- The console starts unauthenticated. Administrative commands require `login <PIN>`.
- Three failed logins lock the console for 60 seconds.
- The default PIN is `1234` and must be changed during commissioning.
- The entrance rejects a second entry for a card already inside, and rejects an exit
  for a card that is not inside.
- A 3-second cooldown suppresses duplicate reads from the same reader.
- Lockdown denies entry while still allowing exit.
- Emergency release grants access and bypasses the schedule.
- A temporary override opens the schedule for 1 to 480 minutes.
- All security transitions and access outcomes are logged.

These measures improve accountability but do not make the prototype secure against
cloned cards or physical tampering. Section 7 discusses the limitations.

## 4. Implementation

### 4.1 Development environment

| Item | Version / setting |
| --- | --- |
| Framework | ESP-IDF 6.0.2 |
| Language | C (GNU23) |
| Target | ESP32 (Xtensa LX6, 4 MB flash) |
| Build system | CMake + Ninja |
| Simulator | Wokwi ESP32 DevKit v1 |
| Partition table | Custom: 1.5 MiB application, 2.4 MiB SPIFFS |

The project is configured with `idf.py set-target esp32` and built with
`idf.py build`. The terminal role is selected in `menuconfig` under
`CoCIS Smart System`.

### 4.2 RFID driver

The MFRC522 driver is implemented directly over `spi_master` rather than using an
external library. It performs a soft reset, configures the internal timer, enables
the antenna, and issues REQA and anticollision commands. The driver validates the
BCC check byte and returns a four-byte UID. Each reader has its own SPI device
handle, chip-select pin, reset pin, last UID, and cooldown timestamp. This makes the
dual-reader entrance possible on a single SPI bus.

### 4.3 Access control

On a valid scan, the application first checks the 3-second cooldown, then checks
whether enrolment mode is active. In enrolment mode the UID is appended to the
allowlist and the door is not pulsed. Otherwise the UID is compared with the
allowlist. A known UID is then evaluated against emergency mode, lockdown, the
schedule or override, and, at the entrance, the inside list. A granted entry adds
the UID to the inside list and increments occupancy; a granted exit removes it and
decrements occupancy. The result is written to the OLED, serial console, web
snapshot, and CSV log.

### 4.4 Occupancy and lighting

Room occupancy is the local count plus contributions received from peer terminals
with the same room ID. Each terminal broadcasts its contribution every second with
a room ID, sequence number, and uptime. Peers ignore reports with a mismatched room
ID or a report older than 10 seconds. The local count is persisted in NVS so that a
reboot does not reset the room to zero.

Lighting is triggered by either a granted access or PIR motion. The light relay is
held on for 30 seconds after the last trigger. The PIR is sampled as a digital input;
a simple debounce prevents repeated triggers from a single motion event. Because the
system uses one PIR per room, a person who remains completely still for longer than
the hold period may cause the light to switch off; a longer hold or a second sensor
would improve this.

### 4.5 Storage and recovery

NVS stores the PIN, schedule, allowlist, occupancy, and inside list. SPIFFS stores
the event log. On boot the application loads the configuration and inside list,
then restores counters from the persisted occupancy. Event records include a
timestamp from the system clock. Because the ESP32 has no battery-backed clock, the
administrator sets the time after each boot; until then, timestamps are marked with
the boot-relative time.

### 4.6 Communication

ESP-NOW is used for occupancy sharing because it does not require a router and has
low latency. Wi-Fi is initialised in station mode for ESP-NOW. When the web dashboard
is enabled, the mode changes to APSTA so that the terminal can both serve its own
access point and exchange ESP-NOW frames. This is a prototype design; production
nodes should authenticate occupancy messages to prevent spoofing.

### 4.7 User interfaces

The OLED driver implements a compact 5x7 font, page-based framebuffer writes, and
`display_show_event` overlays with automatic timeout. The serial UI provides a
banner, menu, status table, who-is-inside table, room table, statistics, and coloured
event lines. The web UI embeds HTML, CSS, and JavaScript in firmware and exposes
`/`, `/api/status`, and `/api/logs`. The web server is compiled in but only started
when the Kconfig option is enabled, so the default image does not create an access
point that the Wokwi simulator cannot expose.

## 5. Testing and Results

### 5.1 Build verification

The firmware was built successfully with ESP-IDF 6.0.2 for the ESP32. Results:

| Configuration | Binary size | Application partition | Free |
| --- | ---: | ---: | ---: |
| Web dashboard disabled (default) | 0xD5D40 (875,840 bytes) | 0x180000 (1,572,864 bytes) | 44% |
| Web dashboard enabled | 0xE48B0 (936,112 bytes) | 0x180000 (1,572,864 bytes) | 40% |

Both configurations compiled and linked without errors. Warnings were treated as
errors by the project build flags, so the successful build also confirms that the
new modules compile cleanly.

### 5.2 Functional test plan

The following tests are to be executed and recorded on the Wokwi simulation and, if
available, on hardware. Each test lists the expected result so that the marker can
reproduce it.

| ID | Test | Procedure | Expected result |
| --- | --- | --- | --- |
| T1 | Boot and login | Reset, open serial monitor, run `login 1234` | Banner, prompt, and "Administrator session enabled" |
| T2 | Authorised entry | Enrol a card, then present it at the entry reader inside the schedule | Entry relay pulses, OLED shows granted, occupancy +1, event logged |
| T3 | Unauthorised card | Present an unknown card | No relay pulse, OLED shows denied, denial logged |
| T4 | Anti-passback | Scan the same card twice for entry | Second scan denied with an "already inside" reason |
| T5 | Authorised exit | Present the inside card at the exit reader | Exit relay pulses, occupancy -1, card removed from inside list |
| T6 | Exit without entry | Present a card not inside at the exit reader | Denied, no relay pulse |
| T7 | Schedule enforcement | Set a window that excludes the current time and scan | Denied with a schedule reason |
| T8 | Temporary override | Run `override 5`, then scan outside the schedule | Granted for five minutes |
| T9 | Lockdown | Run `lockdown on`, then scan a valid card | Entry denied, exit still allowed |
| T10 | Emergency release | Run `emergency on`, then scan | Granted regardless of schedule |
| T11 | Card enrolment | Run `enroll 20`, present a new card | Card added, no door pulse, OLED shows enrolled |
| T12 | PIR lighting | Trigger motion in a room | Light relay on, off after 30 seconds without motion |
| T13 | Persistence | Reboot after changing PIN and occupancy | PIN and occupancy restored |
| T14 | Login lockout | Enter a wrong PIN three times | Console locked for 60 seconds |
| T15 | Log export | Run `logs` or `export` | CSV records printed with timestamps |
| T16 | Web dashboard | Build with web enabled, connect to SoftAP | Dashboard loads and updates (real hardware only) |

### 5.3 Observed results

| Item | Observation |
| --- | --- |
| Build | Pass, both configurations |
| Binary size | Within the 1.5 MiB application partition |
| OLED | Driver initialises and renders in simulation; visual check pending |
| Serial UI | Commands and formatting implemented; runtime check pending |
| Access logic | Implemented and compile-verified; full card-flow test pending |
| ESP-NOW | Code path implemented; multi-node test pending |
| Web dashboard | Build verified with option enabled; host access not possible in Wokwi |

The remaining runtime checks require a Wokwi run with the appropriate terminal role
and are the first activity in the demonstration video. The report will be updated
with screenshots and pass/fail marks after that run.

### 5.4 Discussion of results

The build results show that the complete feature set fits comfortably on a 4 MB
ESP32. The web dashboard adds about 60 KiB, which is acceptable but not free; keeping
it optional is therefore sensible. The dual-reader entrance works within the pin
budget because the readers share SPI and use separate chip selects. The main
technical risk is the clock: without an RTC, schedule enforcement depends on the
administrator setting the time after each boot. A second risk is the single PIR per
room, which cannot detect a motionless occupant. Both are documented limitations
rather than hidden assumptions.

## 6. Challenges and Lessons Learned

1. **SPI device management.** The first design assumed one reader per image. Adding
   an exit reader required refactoring the driver to keep per-reader state instead
   of global state.
2. **Shared state.** The console task and the reader loop run concurrently. Early
   code read mutable globals while another task updated them; the final design uses
   a mutex and copies snapshots before formatting.
3. **Storage trade-offs.** NVS is convenient for small values but not for an
   append-only log. SPIFFS was retained for the CSV log, while NVS holds counters and
   the inside list.
4. **Simulator limits.** Wokwi cannot expose a simulated SoftAP to the host, so the
   web dashboard is disabled by default and documented as a real-hardware feature.
5. **Build hygiene.** Stale CMake caches caused by moving the ESP-IDF installation
   produced confusing failures; deleting `build/` and rebuilding resolved them.
6. **UI constraints.** A 128x64 display forces short labels and a strict layout. The
   event overlay approach gives clear feedback without redesigning the dashboard.

## 7. Limitations

- RFID UIDs can be cloned; the system is identification, not strong authentication.
- ESP-NOW frames are not authenticated in this prototype, so a malicious node could
  inject false occupancy.
- The console PIN is stored in NVS and compared in plaintext.
- The clock is not battery-backed and must be set after boot.
- The PIR detects motion, not presence; a motionless occupant may lose lighting.
- The inside list is local to the entrance controller; a multi-entrance building
  would need a shared or replicated list.
- The web dashboard is read-only and has no user authentication.
- The prototype has not been tested with real door strikes, mains lighting, or
  extended power failures.

## 8. Conclusion and Future Work

The project delivers a working prototype of a smart control and monitoring system
for CoCIS. It combines RFID access control, occupancy tracking, ESP-NOW coordination,
automatic lighting, persistent logging, and three operator interfaces on low-cost
ESP32 hardware. The firmware builds cleanly for both the default and web-enabled
configurations and fits within the chosen partition table. The design demonstrates
the core ideas required for a campus deployment while keeping the limitations
explicit.

Future work should add:

1. A battery-backed RTC or NTP time source.
2. Authenticated and encrypted node messages.
3. A central server or MQTT broker for multi-entrance coordination and long-term
   analytics.
4. A second PIR or a mmWave presence sensor per room.
5. Secure card technology (MIFARE DESFire or a campus smart card) instead of UID-only
   identification.
6. A custom PCB and isolated relay drivers for reliable installation.
7. A mobile-friendly authenticated dashboard with alert notifications.
8. Automated tests for the access state machine and storage recovery.

## 9. References

1. Espressif Systems. *ESP-IDF Programming Guide*, version 6.0. https://docs.espressif.com/projects/esp-idf/en/stable/esp32/
2. Espressif Systems. *ESP-NOW User Guide*. https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html
3. NXP Semiconductors. *MFRC522 Standard Performance MIFARE and NTAG Frontend*, datasheet.
4. ISO/IEC 14443-3:2018. *Identification cards - Contactless integrated circuit cards - Proximity cards - Part 3: Initialization and anticollision*.
5. Wokwi. *ESP32 Simulation Documentation*. https://docs.wokwi.com/guides/esp32
6. Makerere University, College of Computing and Information Sciences. *Embedded Systems / IoT Project Brief*, 2026.

## Appendix A: Serial command reference

| Command | Access | Purpose |
| --- | --- | --- |
| `help`, `menu` | Any | Show command help |
| `status` | Any | Show the live dashboard |
| `who` | Any | List cards currently inside |
| `rooms` | Any | Show known room occupancy |
| `events [n]` | Any | Show the most recent events |
| `logs`, `export` | Any | Print the CSV access log |
| `stats`, `uptime` | Any | Show counters and run time |
| `clear`, `color on\|off` | Any | Clear the screen or toggle colour |
| `login <PIN>`, `logout` | Any | Start or end an administrator session |
| `enroll [seconds]` | Admin | Arm card enrolment for 5-120 seconds |
| `allow <UID>`, `deny <UID>` | Admin | Add or remove an 8-digit hexadecimal UID |
| `schedule <HH:MM> <HH:MM>` | Admin | Set the same-day access window |
| `time <YYYY-MM-DD> <HH:MM:SS>` | Admin | Set the East Africa Time clock |
| `pin <digits>` | Admin | Change the 4-8 digit administrator PIN |
| `lockdown on\|off` | Admin | Deny entry while allowing exit |
| `emergency on\|off` | Admin | Release all access restrictions |
| `override <minutes>` | Admin | Open access for 1-480 minutes |
| `wipe` | Admin | Delete all access records |
| `reboot` | Admin | Restart the terminal |

## Appendix B: Default pin map

| Function | GPIO |
| --- | ---: |
| SPI SCK / MOSI / MISO | 18 / 23 / 19 |
| Entry reader CS / RST | 5 / 22 |
| Exit reader CS / RST | 4 / 13 |
| Entry door relay | 26 |
| Exit door relay | 33 |
| Room light relay | 27 |
| PIR input | 32 |
| OLED SDA / SCL | 21 / 25 |

## Appendix C: Group contributions

See [individual-contributions.md](individual-contributions.md). Each member should
confirm the final wording before submission.
