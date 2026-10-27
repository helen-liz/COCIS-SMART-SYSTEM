# Room Map and System Diagrams

The assignment text identifies Lecture Room 1, Lecture Room 2, Lecture Room 3, and the main entrance, but provides no architectural floor plan. These are logical room assignments, not a measured building layout. Replace the illustrative room labels/positions with the approved CCIS floor plan before installation.

## Logical room map

```text
CCIS BUILDING
┌────────────────────┐  ┌────────────────────┐  ┌────────────────────┐
│ Lecture Room 1     │  │ Lecture Room 2     │  │ Lecture Room 3     │
│ Room ID 1          │  │ Room ID 2          │  │ Room ID 3          │
│ Entry + Exit RFID  │  │ Entry + Exit RFID  │  │ Entry + Exit RFID  │
│ ESP32 / light relay│  │ ESP32 / light relay│  │ ESP32 / light relay│
└────────────────────┘  └────────────────────┘  └────────────────────┘
             Main Entrance: Room ID 0, separate perimeter access terminals
```

## System topology

```mermaid
flowchart LR
    Admin[Administrator at serial console]
    subgraph R1[Lecture Room 1 - Room ID 1]
        R1IN[Entry ESP32 + RFID]
        R1OUT[Exit ESP32 + RFID]
        R1REL[Door relay]
        R1LIGHT[Room light relay]
        R1IN --> R1REL
        R1OUT --> R1REL
        R1IN -. signed occupancy .-> R1COUNT[Room 1 occupancy]
        R1OUT -. signed occupancy .-> R1COUNT
        R1COUNT --> R1LIGHT
    end
    subgraph R2[Lecture Room 2 - Room ID 2]
        R2IN[Entry ESP32 + RFID]
        R2OUT[Exit ESP32 + RFID]
        R2REL[Door relay]
        R2LIGHT[Room light relay]
        R2IN --> R2REL
        R2OUT --> R2REL
        R2IN -. signed occupancy .-> R2COUNT[Room 2 occupancy]
        R2OUT -. signed occupancy .-> R2COUNT
        R2COUNT --> R2LIGHT
    end
    subgraph R3[Lecture Room 3 - Room ID 3]
        R3IN[Entry ESP32 + RFID]
        R3OUT[Exit ESP32 + RFID]
        R3REL[Door relay]
        R3LIGHT[Room light relay]
        R3IN --> R3REL
        R3OUT --> R3REL
        R3IN -. signed occupancy .-> R3COUNT[Room 3 occupancy]
        R3OUT -. signed occupancy .-> R3COUNT
        R3COUNT --> R3LIGHT
    end
    ENTRANCE[Main entrance terminals - Room ID 0]
    Admin -->|serial admin commands| R1IN
    Admin -->|serial admin commands| R2IN
    R1IN -. ESP-NOW, same room ID .-> R1OUT
    R1OUT -. ESP-NOW, same room ID .-> R1IN
    R2IN -. ESP-NOW, same room ID .-> R2OUT
    R2OUT -. ESP-NOW, same room ID .-> R2IN
    R3IN -. ESP-NOW, same room ID .-> R3OUT
    R3OUT -. ESP-NOW, same room ID .-> R3IN
```

Each ESP32 broadcasts a signed count with its room ID. A terminal aggregates only matching room IDs, so room lighting is based on that room's entry/exit count rather than the whole building. The pictured main-entrance node is a separate access zone; it does not determine which lecture room a visitor enters.

## Room-terminal wiring

```mermaid
flowchart LR
    ESP32[ESP32 DevKit]
    RFID[MFRC522 RFID reader]
    DOOR[Isolated door relay driver]
    LIGHT[Isolated light relay driver]
    STRIKE[Door strike / lock supply]
    LAMP[Room lighting supply]
    ESP32 -->|GPIO 18 SCK, 23 MOSI, 19 MISO| RFID
    ESP32 -->|GPIO 5 CS, GPIO 22 RST| RFID
    ESP32 -->|GPIO 26 output| DOOR
    DOOR --> STRIKE
    ESP32 -->|GPIO 27 output| LIGHT
    LIGHT --> LAMP
    ESP32 ---|common low-voltage signal ground| RFID
```

Use the GPIO defaults in `main/main.c` only after checking the exact board and module pinout. Relay outputs are active-high in this prototype. Do not connect a door strike, mains voltage, or other high-current load directly to an ESP32 GPIO.