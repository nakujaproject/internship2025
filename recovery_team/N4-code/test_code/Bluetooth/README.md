# ESP32 ↔ HC-05 Bluetooth Module Setup Documentation

## 1. Overview

This documentation covers the setup, interfacing, and configuration of a classic Bluetooth module (HC-05/HC-06) using an ESP32 UART bridge. The goal is to send and receive AT commands directly from a terminal via the ESP32, allowing for easy configuration of the module's name, password, baud rate, and role.

The ESP32 communicates with the HC-05 using **UART2** on pins:

* **TX → HC-05 RX** (GPIO17)
* **RX ← HC-05 TX** (GPIO16)

A USB-TTL converter or Serial Monitor is used to send commands from a PC to the ESP32.

---

## 2. Common Mistakes Encountered

During the initial setup, several issues were encountered:

1. **Not holding the EN/KEY pin high**

   * The HC-05 must have its **EN/KEY pin HIGH** to enter AT mode.
   * Without this, the module defaults to normal Bluetooth mode and ignores AT commands.

2. **Incorrect baud rate**

   * AT mode typically uses **38400 baud** (HC-05) or **9600 baud** (HC-06).
   * Sending commands at the wrong baud results in no response or garbage characters.

3. **Sending commands without proper line endings**

   * AT commands must end with `\r\n` (carriage return + newline).
   * Failing to include this can cause commands to be ignored.

4. **Blocking Serial reads**

   * Using `Serial.read()` for every character prints messy output and can drop characters.
   * Buffering lines until a newline gives a clear output.

5. **Delays and timing issues**

   * Sending multiple commands too quickly may cause HC-05 to ignore later commands.
   * Recommended: 0.5–1 second between commands.

---

## 3. AT Commands Reference

### 3.1 Password (PIN)

| Module | Default PIN  | Check Command | Set Command    |
| ------ | ------------ | ------------- | -------------- |
| HC-05  | 1234         | `AT+PSWD?`    | `AT+PSWD=1234` |
| HC-06  | 1234 or 0000 | Not supported | `AT+PIN1234`   |

---

### 3.2 Module Name

* Check current name:

```text
AT+NAME?
```

* Set new name:

```text
AT+NAME=N4_Base_BT
```

---

### 3.3 Role (Master/Slave)

* Check role:

```text
AT+ROLE?
```

* Set role:

```text
AT+ROLE=0   // Slave
AT+ROLE=1   // Master
```

---

### 3.4 UART / Baud Rate

* Check current UART:

```text
AT+UART?
```

* Set new UART:

```text
AT+UART=9600,0,0   // baud 9600, 1 stop bit, no parity
AT+UART=38400,0,0  // baud 38400
AT+UART=115200,0,0 // baud 115200 (for N4 Base Station)
```

---

### 3.5 Other Useful Commands

| Command         | Description                               |
| --------------- | ----------------------------------------- |
| `AT`            | Test if module responds                   |
| `AT+VERSION?`   | Query firmware version                    |
| `AT+RESET`      | Restart the module                        |
| `AT+ORGL`       | Restore factory defaults                  |
| `AT+CMODE=0/1`  | Auto-connect mode (0 = specific, 1 = any) |
| `AT+BIND=<MAC>` | Bind master to a specific device          |

---

## 4. Circuit Connections

| ESP32 Pin   | Connection              | Notes                              |
| ----------- | ----------------------- | ---------------------------------- |
| GPIO17 (TX) | HC-05 RX                | ESP32 transmits to HC-05           |
| GPIO16 (RX) | HC-05 TX                | ESP32 receives from HC-05          |
| GND         | GND                     | Common ground                      |
| 3.3V / 5V   | VCC (HC-05)             | Power supply for HC-05             |
| EN / KEY    | 3.3V (to enter AT mode) | Hold HIGH during power-on for AT mode |

> **Note:** Ensure voltage compatibility — HC-05 RX typically requires 3.3V logic. Use a voltage divider or level shifter if powered at 5V.

---

## 5. ESP32 UART Bridge Code

This code allows sending AT commands to the HC-05 via the Serial Monitor and prints both **sent** and **received** messages clearly:

```cpp
#include <HardwareSerial.h>

HardwareSerial BTSerial(2);

#define BT_RX 16
#define BT_TX 17

String rxBuffer = "";  // buffer for incoming BT data

void setup() {
  Serial.begin(115200);
  delay(1000);

  // HC-05 AT mode default baud = 38400
  BTSerial.begin(38400, SERIAL_8N1, BT_RX, BT_TX);

  Serial.println("=== ESP32 ↔ Bluetooth UART Monitor ===");
  Serial.println("RX: GPIO16  TX: GPIO17");
  Serial.println("BT Baud: 38400");
  Serial.println("Line Ending: Both NL & CR");
  Serial.println("=====================================");
}

void loop() {
  // ---------------------
  // Forward from PC → BT
  // ---------------------
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n'); // read a full line
    cmd.trim();                                // remove \r or whitespace

    if (cmd.length() > 0) {
      Serial.print("[TX → BT] ");
      Serial.println(cmd);                     // print nicely

      BTSerial.print(cmd);                     // send command
      BTSerial.print("\r\n");                  // ensure AT command line ending
    }
  }

  // ---------------------
  // Forward from BT → PC
  // ---------------------
  while (BTSerial.available()) {
    char c = BTSerial.read();
    rxBuffer += c;

    // Print full line once we get newline
    if (c == '\n') {
      rxBuffer.trim();                         // remove \r and \n
      if (rxBuffer.length() > 0) {
        Serial.print("[RX ← BT] ");
        Serial.println(rxBuffer);
      }
      rxBuffer = "";                           // reset buffer
    }
  }
}
```

---

## 6. How to Use the Bridge

1. Power the HC-05 with **KEY/EN HIGH** to enter AT mode.
2. Connect ESP32 TX → HC-05 RX, ESP32 RX ← HC-05 TX.
3. Upload the bridge code to ESP32.
4. Open Serial Monitor (115200 baud, "Both NL & CR" line ending).
5. Type AT commands like `AT`, `AT+NAME?`, `AT+PSWD=1234` etc.
6. See responses in `[RX ← BT]` lines.

### Example Configuration Session

```text
[TX → BT] AT
[RX ← BT] OK

[TX → BT] AT+NAME?
[RX ← BT] +NAME:HC-05
[RX ← BT] OK

[TX → BT] AT+NAME=N4_Base_BT
[RX ← BT] OK

[TX → BT] AT+PSWD?
[RX ← BT] +PSWD:1234
[RX ← BT] OK

[TX → BT] AT+UART=115200,0,0
[RX ← BT] OK

[TX → BT] AT+RESET
[RX ← BT] OK
```

---

## 7. Troubleshooting Guide

### 7.1 Common Issues & Fixes

| Issue                                          | Possible Cause                           | Solution                                                                                                   |
| ---------------------------------------------- | ---------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| **No response from `AT` command**              | HC-05 not in AT mode (EN/KEY not HIGH)   | Hold EN/KEY HIGH while powering the module. LED should blink slowly (~2s interval).                        |
|                                                | Wrong baud rate                          | Try 38400 (HC-05) or 9600 (HC-06). Change ESP32 UART accordingly.                                          |
| **Garbled characters on Serial Monitor**       | Mismatch between module baud and ESP32   | Match HC-05 baud (AT mode default: 38400) in `BTSerial.begin()`.                                           |
| **Module ignores commands**                    | Missing `\r\n` at the end of AT commands | Ensure bridge adds `\r\n` after each command.                                                              |
| **Cannot change name or password**             | Module already paired / connected        | Disconnect module from any device. Reset if needed with `AT+RESET` or power cycle.                         |
| **LED blinking fast / continuously**           | Normal mode active                       | Module is not in AT mode. Power off, hold EN/KEY HIGH, power on again.                                     |
| **Commands appear once, then stop responding** | Buffer overflow or missed line endings   | Use full line buffering in the code (as in the ESP32 bridge). Avoid sending multiple commands too quickly. |

---

### 7.2 LED Status Patterns

| LED Blink Pattern            | Meaning                               |
| ---------------------------- | ------------------------------------- |
| Slow blink (~2s interval)    | AT command mode active                |
| Fast blink (~0.5s interval)  | Normal Bluetooth mode, ready to pair  |
| Very fast or irregular blink | Module in error or paired to a device |

---

### 7.3 Recovery Tips

1. **Incorrect Baud**

   * Connect module via USB-TTL or ESP32 UART.
   * Try common AT mode baud rates: 38400 (HC-05) or 9600 (HC-06).
   * Adjust `BTSerial.begin(baud, SERIAL_8N1, RX, TX);` accordingly.

2. **Module Not Responding**

   * Power cycle the module.
   * Hold **EN/KEY HIGH** for AT mode.
   * Reset ESP32 if needed.

3. **Lost Configuration**

   * Restore factory defaults:

   ```text
   AT+ORGL
   ```

   * Reset module after changes:

   ```text
   AT+RESET
   ```

4. **Command Timing**

   * Leave ~0.5–1 second between AT commands.
   * Avoid flooding the module with multiple commands too quickly.

---

### 7.4 Best Practices

* Always check the LED blink before sending AT commands.
* Use buffered line reads in the ESP32 code to avoid dropped characters.
* Keep a small cheat sheet of common AT commands and expected responses.
* Power module with stable 3.3V–5V supply; unstable power can cause intermittent failures.
* For production use (N4 Base Station), configure baud to 115200 to match operational requirements.

---

## 8. N4 Base Station Configuration

For the N4 rocket recovery system base station, the HC-05 should be configured using the following naming paradigm:

### Naming Convention
- **Rocket 1 / Base Station 1**: Name = `N4_Base_BT_0001`, Password = `0001`
- **Rocket 2 / Base Station 2**: Name = `N4_Base_BT_0002`, Password = `0002`
- **Rocket N / Base Station N**: Name = `N4_Base_BT_000N`, Password = `000N`

This allows multiple rocket-base station pairs to operate independently without interference.

### Configuration Commands

**For Rocket/Base Station 1:**
```text
AT+NAME=N4_Base_BT_0001
AT+PSWD=0001
AT+UART=115200,0,0
AT+ROLE=0
AT+RESET
```

**For Rocket/Base Station 2:**
```text
AT+NAME=N4_Base_BT_0002
AT+PSWD=0002
AT+UART=115200,0,0
AT+ROLE=0
AT+RESET
```

**For Additional Base Stations:**
Replace the number in both name and password (e.g., 0003, 0004, etc.)

### Configuration Parameters
- **Name**: N4_Base_BT_000X (unique identification per rocket/base station)
- **Password**: 000X (4-digit matching the rocket/base station number)
- **Baud Rate**: 115200 (matches base station UART)
- **Role**: Slave (allows mobile devices to connect)

### Important Notes
- Always use matching numbers for name and password
- Each rocket should have its dedicated base station number
- Recommended format: 4-digit with leading zeros (0001-9999)
- After configuration, power cycle the module **without EN/KEY HIGH** to enter normal Bluetooth mode for operational use

---

## 9. Files in This Directory

- `Bluetooth.ino` - ESP32 UART bridge code for HC-05 configuration
- `README.md` - This documentation file

---

**Last Updated**: January 11, 2026  
**Project**: N4 Flight Computer - Recovery Team  
**Module**: HC-05 Bluetooth Configuration
