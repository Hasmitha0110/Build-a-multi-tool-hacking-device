# Multi-Tool Hacking Device

## About the Project
This project is an ESP32-based multi-tool designed for hardware security testing and cybersecurity demonstrations. It features a standalone web-based interface that allows users to interact with RFID/NFC systems and sub-GHz radio frequencies. To demonstrate these capabilities in a practical scenario, the repository also includes the code for two mock target devices: an RFID-based door lock and a car remote signal receiver.

## What Can This App Do?
The device acts as a portable, standalone hacking tool hosted entirely on an ESP32. By connecting to the device's Wi-Fi Access Point (e.g., `ESP32-MultiTool` or `RFID-Hacker-AP`), you can access a web dashboard to perform the following operations:

- **RFID/NFC Scanning & Saving**: Read UIDs from physical cards (like MIFARE Classic or MIFARE Ultralight/NTAG). The tool automatically saves the captured UIDs, card types, and timestamps to an internal database.
- **Card Emulation**: Select any captured UID from the web interface and command the PN532 module to emulate it. This allows you to "become" the card and bypass RFID locks without possessing the physical tag.
- **Magic Card Writing (Gen1A)**: Write captured UIDs to writable Magic Gen1A tags (Block 0 rewrite) directly from the web interface, creating a physical clone of the original card.
- **Sub-GHz RF Jamming**: Use the CC1101 transceiver to transmit noise at 315MHz. This can jam signals from remote controls, such as older car key fobs or garage door openers.
- **Manage Database**: View, interact with, and delete saved cards from the ESP32's memory using the built-in JSON API.

### Included Target Demonstrations
To test the multi-tool, the project includes firmware for two target devices:
1. **RFID Door Lock (`Doar_Key_Lock.ino`)**: An Arduino-based mock lock using an MFRC522 reader, an I2C LCD screen, a relay, and a buzzer. It can be unlocked by authorized physical cards or by the ESP32 emulating an authorized card.
2. **Car Signal Receiver (`Car_Design.ino`)**: A simple receiver setup with LEDs representing Lock, Unlock, Trunk, and Alarm states. It is used to demonstrate the effects of the 315MHz RF jammer.

## Technical Details & Hardware Stack

The multi-tool is built to efficiently manage multiple hardware peripherals using different hardware buses simultaneously. 

### Microcontrollers
- **ESP32**: The core of the multi-tool. It hosts the Access Point, the Web Server, handles REST API routing, and runs concurrent tasks (e.g., FreeRTOS tasks for emulation). It utilizes both VSPI and HSPI hardware blocks to prevent SPI conflicts between the PN532 and CC1101 modules.
- **Arduino (Uno/Nano)**: Used to run the isolated target devices (`Doar_Key_Lock` and `Car_Design`).

### Hardware Modules
- **PN532 RFID/NFC Module**: Connected via SPI (HSPI). Responsible for reading target tags, writing to Magic cards, and running the card emulation loop.
- **CC1101 RF Transceiver (HW-863)**: Connected via SPI (VSPI). Configured for ASK/OOK modulation at 315MHz. It transmits a continuous noise pattern when the jammer is activated.
- **MFRC522 RFID Reader**: Used on the target door lock side to detect the ESP32's emulated signal or physical tags.
- **Peripherals**: I2C LCD (16x2), 5V Relay, Active Buzzer, and indicator LEDs.

### Software & Libraries
- **Web & Networking**: Native `WiFi.h` and `WebServer.h` for serving the HTML/JS frontend and handling HTTP GET requests.
- **API & Data**: `ArduinoJson` is used extensively to pass data between the web frontend and the ESP32 backend.
- **RFID Control**: `PN532`, `PN532_SPI`, and `emulatetag` libraries handle the low-level APDU commands, SAM configuration, and MIFARE authentication.
- **RF Control**: `ELECHOUSE_CC1101_SRC_DRV` library is used to initialize the CC1101 module, set transmission frequencies, and toggle between Tx (Transmit/Jam) and Rx modes.
- **Multitasking**: Uses FreeRTOS (`xTaskCreatePinnedToCore`) to pin the emulation loop to a specific CPU core, ensuring the web server remains responsive during active emulation.
