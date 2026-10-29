# System Architecture and Diagrams

The pilot covers the CoCIS main entrance and two lecture rooms. No architectural
floor plan was supplied, so the room positions below are logical, not measured.
Replace them with the approved CoCIS layout before installation.

## Logical layout

```text
COCIS BUILDING
┌──────────────────────┐      ┌──────────────────────┐
│ Lecture Room 1       │      │ Lecture Room 2       │
│ Room ID 1            │      │ Room ID 2            │
│ RFID + PIR + light   │      │ RFID + PIR + light   │
│ OLED + serial + web  │      │ OLED + serial + web  │
└──────────┬───────────┘      └──────────┬───────────┘
           │        ESP-NOW occupancy    │
           └──────────────┬──────────────┘
                          │
                ┌─────────┴──────────┐
                │ Main entrance      │
                │ Room ID 0          │
                │ Entry + exit RFID  │
                │ Two door relays    │
                │ OLED + serial + web│
                └────────────────────┘
```

Each terminal is a self-contained ESP32 node. The entrance has both entry and exit
readers on one controller; each room has one reader for access and a PIR sensor for
lighting.

## Node topology

```mermaid
flowchart LR
    ADMIN[Administrator] -->|USB serial console| ENTRY
    ADMIN -->|USB serial console| R1
    ADMIN -->|USB serial console| R2

    subgraph ENTRANCE[Main entrance - Room ID 0]
        ENTRY[ESP32 controller]
        EIN[Entry MFRC522]
        EOUT[Exit MFRC522]
        EREL1[Entry door relay GPIO 26]
        EREL2[Exit door relay GPIO 33]
        EOLED[SSD1306 dashboard]
        EIN --> ENTRY
        EOUT --> ENTRY
        ENTRY --> EREL1
        ENTRY --> EREL2
        ENTRY --> EOLED
    end

    subgraph ROOM1[Lecture Room 1 - Room ID 1]
        R1[ESP32 controller]
        R1RFID[MFRC522]
        R1PIR[PIR GPIO 32]
        R1LIGHT[Light relay GPIO 27]
        R1OLED[SSD1306 dashboard]
        R1RFID --> R1
        R1PIR --> R1
        R1 --> R1LIGHT
        R1 --> R1OLED
    end

    subgraph ROOM2[Lecture Room 2 - Room ID 2]
        R2[ESP32 controller]
        R2RFID[MFRC522]
        R2PIR[PIR GPIO 32]
        R2LIGHT[Light relay GPIO 27]
        R2OLED[SSD1306 dashboard]
        R2RFID --> R2
        R2PIR --> R2
        R2 --> R2LIGHT
        R2 --> R2OLED
    end

    BROWSER[Phone or laptop] -. optional Wi-Fi AP .-> ENTRY
    BROWSER -. optional Wi-Fi AP .-> R1
    BROWSER -. optional Wi-Fi AP .-> R2
    ENTRY <-. ESP-NOW room counts .-> R1
    R1 <-. ESP-NOW room counts .-> R2
    R2 <-. ESP-NOW room counts .-> ENTRY
```

The web dashboard is optional and disabled by default. Enable it per terminal with
`CONFIG_SMART_SYSTEM_ENABLE_WEB`; the node then starts a SoftAP and serves the
dashboard at `http://192.168.4.1`.

## Component view

```mermaid
flowchart TB
    subgraph APP[Application layer]
        MAIN[main.c orchestration]
        UI[ui.c serial console]
        DISPLAY[display.c OLED UI]
        WEB[webui.c HTTP dashboard]
    end
    subgraph DOMAIN[Domain services]
        ACCESS[Access control and anti-passback]
        LIGHT[Occupancy and lighting control]
        SECURITY[PIN, lockdown, emergency, override]
    end
    subgraph PLATFORM[Platform services]
        STORAGE[storage.c NVS + SPIFFS]
        COMMS[communication.c ESP-NOW + Wi-Fi]
        RFID[SPI MFRC522 driver]
        GPIO[GPIO relays and PIR]
    end

    UI --> MAIN
    DISPLAY --> MAIN
    WEB --> MAIN
    MAIN --> ACCESS
    MAIN --> LIGHT
    MAIN --> SECURITY
    ACCESS --> STORAGE
    LIGHT --> GPIO
    ACCESS --> COMMS
    COMMS --> STORAGE
    RFID --> ACCESS
    SECURITY --> STORAGE
```

`main.c` owns the shared state and a FreeRTOS mutex. The UI modules never touch
hardware directly; they read snapshots produced by the application layer.

## Card-scan sequence

```mermaid
sequenceDiagram
    participant Card
    participant Reader as MFRC522
    participant App as ESP32 application
    participant Store as NVS / SPIFFS
    participant Door as Relay
    participant UI as OLED / serial / web
    participant Peer as ESP-NOW peers

    Card->>Reader: Present UID
    Reader->>App: SPI UID read
    App->>App: Validate UID, schedule, mode, inside list
    App->>Store: Append access event
    alt Access granted
        App->>Door: Pulse relay
        App->>Store: Persist inside list / occupancy
        App->>UI: Show ACCESS GRANTED and event
        App->>Peer: Broadcast occupancy contribution
    else Access denied
        App->>UI: Show ACCESS DENIED with reason
    end
```

## Access decision logic

```mermaid
flowchart TD
    SCAN[Card UID received] --> DUP{Scanned within 3 s?}
    DUP -->|yes| IGNORE[Ignore duplicate]
    DUP -->|no| ENROLL{Enrollment mode?}
    ENROLL -->|yes| SAVE[Save UID to allowlist and exit]
    ENROLL -->|no| KNOWN{UID on allowlist?}
    KNOWN -->|no| DENY[Deny and log]
    KNOWN -->|yes| MODE{Emergency or lockdown?}
    MODE -->|emergency| GRANT[Grant]
    MODE -->|lockdown| DENY
    MODE -->|normal| SCHED{Within schedule or override?}
    SCHED -->|no| DENY
    SCHED -->|yes| PASSBACK{Entrance anti-passback satisfied?}
    PASSBACK -->|no| DENY
    PASSBACK -->|yes| GRANT
    GRANT --> PULSE[Pulse correct door relay]
    GRANT --> UPDATE[Update occupancy and inside list]
```

The entrance checks the inside list: a card already inside cannot enter again, and
a card not inside cannot exit. Lecture rooms do not use anti-passback because they
have a single reader.

## Room-terminal wiring

```mermaid
flowchart LR
    ESP32[ESP32 DevKit]
    RFID[MFRC522 reader]
    PIR[PIR motion sensor]
    DOOR[Isolated door relay]
    LIGHT[Isolated light relay]
    OLED[SSD1306 OLED]
    ESP32 -->|GPIO 18 SCK, 23 MOSI, 19 MISO| RFID
    ESP32 -->|GPIO 5 CS, GPIO 22 RST| RFID
    ESP32 -->|GPIO 32 input| PIR
    ESP32 -->|GPIO 26 output| DOOR
    ESP32 -->|GPIO 27 output| LIGHT
    ESP32 -->|GPIO 21 SDA, GPIO 25 SCL| OLED
```

## Main-entrance wiring

```mermaid
flowchart LR
    ESP32[ESP32 DevKit]
    ENTRY[MFRC522 entry]
    EXIT[MFRC522 exit]
    DOOR1[Entry door relay GPIO 26]
    DOOR2[Exit door relay GPIO 33]
    OLED[SSD1306 OLED]
    ESP32 -->|shared SPI, CS 5, RST 22| ENTRY
    ESP32 -->|shared SPI, CS 4, RST 13| EXIT
    ESP32 -->|GPIO 26| DOOR1
    ESP32 -->|GPIO 33| DOOR2
    ESP32 -->|I2C SDA 21, SCL 25| OLED
```

Both readers share SCK, MOSI, and MISO but have independent chip-select and reset
lines. The relays are active-high and must drive an isolated door-strike circuit.
Never connect a strike or mains voltage directly to an ESP32 pin.

## Data model

```mermaid
erDiagram
    TERMINAL ||--o{ ACCESS_EVENT : writes
    TERMINAL ||--|| CONFIG : stores
    TERMINAL ||--o{ INSIDE_RECORD : tracks
    TERMINAL ||--|| OCCUPANCY : reports

    CONFIG {
        string pin
        string schedule_start
        string schedule_end
        uint8 occupancy
    }
    ACCESS_EVENT {
        string timestamp
        string uid
        string room
        string decision
        string reason
    }
    INSIDE_RECORD {
        string uid
        string room
        uint32 entered_at
    }
    OCCUPANCY {
        uint8 room_id
        uint16 count
        uint32 sequence
    }
```

`CONFIG` and `INSIDE_RECORD` live in NVS. `ACCESS_EVENT` is appended to
`/spiffs/events.csv`. Occupancy is persisted in NVS and also broadcast over ESP-NOW.
