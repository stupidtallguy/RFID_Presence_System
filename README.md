# RFID Presence System (ESP32 + MFRC522 + MQTT)

This project implements a student presence tracking system using an ESP32, an MFRC522 RFID reader and MQTT. When a user presents a registered card the device publishes a log message to a remote server and triggers a WLED color assigned to that user. The system stores user data and the Admin card UID in the ESP32 Flash (Preferences) so data persists across reboots.

## Features
- Read RFID (MFRC522) cards and validate against stored users
- Admin/master card to add users and lock/unlock the device
- Publish logs to an MQTT broker (configurable)
- RGB LED visual feedback and WLED integration using per-user HEX color
- Persistent storage of users and admin UID in Flash
- Factory-reset button to erase stored data and return to First Boot

<img width="878" height="656" alt="image" src="https://github.com/user-attachments/assets/3d287629-e658-4079-977a-8f9c68db92d5" />

## Components
1. ESP32 development board (USB cable for programming)
2. MFRC522 RFID reader
3. Push buttons (x2) — Admin and Reset
4. RGB LED (common VCC)
5. Jumper wires / breadboard

## Wiring / Pin mapping (ESP32)
RFID (MFRC522) -> ESP32
- 3.3V -> 3.3V
- RST -> GPIO 21
- GND -> GND
- MISO -> GPIO 19
- MOSI -> GPIO 23
- SCK -> GPIO 18
- SDA (SS) -> GPIO 5

RGB LED
- VCC (common) -> 3.3V
- Red -> GPIO 17
- Green -> GPIO 4
- Blue -> GPIO 22

Buttons
- Admin button -> GPIO 15 (configured with internal pull-down; button connects to 3.3V when pressed)
- Reset button -> GPIO 16 (factory reset; configured with internal pull-down; button connects to 3.3V when pressed)

> Note: Buttons rely on internal pull-downs in software (so wire them between the input pin and 3.3V).

## Breadboard / Schematic description
Use the provided breadboard schematic as the wiring reference. The schematic shows the MFRC522 module on the left, the ESP32 on the right and the RGB LED + two push buttons wired on the breadboard. Important points:
- MFRC522 is powered from 3.3V and shares GND with the ESP32.
- SPI lines (MOSI/MISO/SCK/SS) connect from the MFRC522 to the ESP32 pins listed above.
- RGB LED uses a common VCC (3.3V) and three GPIOs for color channels (R/G/B). Use appropriate resistors if your LED requires them.
- Buttons are wired so pressing the button connects the GPIO to 3.3V; internal pull-downs keep the line low when unpressed.

If you want, add the breadboard image in the repository (e.g., `schematic.png`) so visitors can visually confirm wiring.



<img width="1001" height="617" alt="image" src="https://github.com/user-attachments/assets/931496c8-05ad-409d-be0d-14474b3832cb" />


## System Modes (finite state machine)
1. First Boot
- Run only on the first power-up (or after factory reset). The first presented card becomes the Admin card and its UID is saved to Flash. Then the system goes to Sleep Mode.
2. Sleep Mode
- The default locked state. Normal user cards are ignored. Presenting the Admin card wakes the device to Idle Mode.
3. Idle Mode (Operational)
- Normal operation: when a registered card is presented a log is published to MQTT and the WLED color for that user is triggered. Presenting the Admin card locks the device (returns to Sleep Mode).
4. Waiting Mode
- Entered when the Admin button is pressed. The device waits up to 5 seconds for the Admin card to authenticate; success → Admin Mode; timeout → Idle Mode.
5. Admin Mode
- Register new users: Admin presents a new card, the system prompts for a Group Name and HEX color over serial, then publishes user info to the server and stores it in Flash. After registering (or a 5s timeout) the device returns to Idle Mode.

## MQTT & Data format
- Configure the MQTT broker and topics in `include/config.h` (or the project configuration file).
- Example payload when logging an existing user (example JSON):

```
{"uid":"A1:B2:C3:D4","verify":"randomString123"}
```

- Example payload when registering a new user (published by Admin Mode):

```
{"uid":"A1:B2:C3:D4","name":"John Doe","color":"#FF8800"}
```

Adjust the exact topic names and payloads to match the server-side expectations; edit `config.h` accordingly.

## Persistence & Factory Reset
- User records (UID, name, color) and the Admin UID are stored in the ESP32 Preferences (Flash) and persist across power cycles.
- To perform a factory reset (erase all users and admin), press the Reset button wired to GPIO 16 (this triggers a full wipe and returns the device to First Boot state).

## Building & Uploading
This project uses PlatformIO. From the project folder (`RFID/`) you can:

```bash
# Build
platformio run

# Upload to the ESP32 (make sure the board is in boot mode or use the auto open/flash available)
platformio run --target upload

# Open serial monitor (default baud 115200)
platformio device monitor --baud 115200
```

Or use the PlatformIO extension in VS Code to build, upload and open the serial monitor.

## Configuration
- Edit `include/config.h` to set your MQTT broker address, topics, WLED endpoint, and any other configurable values.

## Notes / Troubleshooting
- Ensure the MFRC522 module runs at 3.3V (do NOT power it from 5V when connected to the ESP32 pins).
- If RFID reads fail, verify SPI pin wiring (MOSI/MISO/SCK/SS) and that `SDA`/`SS` pin matches in code.
- Use the serial monitor to view prompts and debug logs (Admin registration prompts appear on Serial).

## Credits
Shiraz University — Microcontroller Lab Project (Fall-2025)

