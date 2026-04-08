# Smart Locker Project

A secure "battery-powered" smart locker system built on a standalone ATmega328p microcontroller. 
Features numeric-locking, encrypted password storing, idle/sleep mode for power efficency, and 
low battery alerts. This is all being made without the use of external libraries.

***

## Features
   - SG90 Servo motor control, made with a custom PWM servo library, using Timer1 interrupts
   - 12-key numeric keypad, made with a resistor ladder
   - Finite State Machine (_FSM_) manages all system states, such as, _LOCKED, UNLOCKED, SLEEP, PASSKEY CHECKING, etc._
   - Power consumption efficiency through our sleep mode, works using SMCR registers and wakes on INT0 hardware interrupts
   - No hard-coded passkeys, password encryption with a simple XOR checksum with random (_rand()_) salt
   - Overcurrent detection/protection, if rotor is jammed, program will detect that via shunt resistor
   - LED feedback
   - Low battery alerting, LEDs flash when battery falls below threshold

## Hardware

### Components

| Component | Purpose |
|---|---|
| ATmega328p/Arduino Uno | Microcontroller |
| SG90 Servo | Lock/unlock tool |
| 3×4 Matrix Keypad | User input device |
| MCP1702 LDO Regulator / Voltage | 9V → 5V regulation |
| 16 MHz Crystal & 2× 22pF caps | External clock |
| 10kΩ pull-up resistor | Reset pin stabilization |
| 0.1 µF capacitor | AREF decoupling (pin 21) |
| 1Ω shunt resistor (_better option is a 0.1Ω_) | Overcurrent / block detection |
| 100kΩ + 47kΩ resistors | Battery voltage divider |
| 10kΩ pull-down resistor | Keypad ADC stabilization |
| Red, Green, Yellow LEDs | State feedback |
| 9V battery | Power source |
| Wiring | For connectivity between parts |
| 9V battery | Power source |

<br>

![PSLP Schematic Image](schematics/locker-schematic-image.png)

### Pin Mapping (ATmega328p)

| Pin | Function |
|---|---|
| D2 (INT0) | Keypad wake interrupt |
| D3, D4, D5 | Keypad columns (C1–C3) |
| D10 (OC1B) | Servo PWM |
| D12 | Red LED |
| D13 | Green LED |
| A0 | Keypad row ADC |
| A1 | Servo shunt current sense |
| A2 | Battery voltage divider |

## System Architecture

### Keypad Resistor Ladder

To avoid the idea of it being expensive and inefficient for these keypads to have a set of wires 
for each button, all 4 rows connect to a single analog pin (A0) through a resistor ladder. Each 
row produces a unique voltage and by comparing the measured analog values against the threshold 
for each row, we can determine which row the pressed key belongs to. The combination of the 
active column and the row voltage identifies the specific key that was pressed.

| Row | Target Voltage | Resistor | ADC Threshold |
|---|---|---|---|
| R1 | 4.5V | 1.1kΩ | ~922 |
| R2 | 3.5V | 4.3kΩ | ~715 |
| R3 | 2.5V | 10kΩ | ~512 |
| R4 | 1.5V | 22kΩ | ~320 |

<br>

### Battery Life

The system is powered by a 9V battery regulated to 5V via the MCP1702. Since the regulator 
maintains a constant 5V, the microcontroller can't directly measure battery health from VCC. 
A voltage divider (100kΩ + 47kΩ) on A2 scales the battery voltage down to a safe ADC range. 
The low battery threshold is set at 7V, giving an early warning before the regulator drops out 
around 6V. When below that threshold a yellow LED flashes three times.

| Mode | Current Draw | Battery Life (550 mAh) |
|---|---|---|
| Active | ~4.32 mA | — |
| Sleep | ~0.18 mA | ~127 days |
| Combined (2 wake-ups/day) | ~4.36 mAh/day | ~126 days |

### Servo PWM (Timer1)

For our servo implementation we have 4 functions: servoAttach, servoDetach, servoWrite, and 
servoRead. These are made with zero external libraries as that was one of our constraints. Servo 
control is done through hardware Timer1 in fast PWM mode on pin 10 due to its 16 bit superior 
resolution. Below are the formulas and values used:

```c
TOP  (ICR1)  = (f_clk / f_PWM / N) - 1 = (16MHz / 50Hz / 8) - 1 = 39999
OCR1B (0°)   = 3000  →  1.5ms pulse (neutral)
OCR1B (90°)  = 4000  →  2.0ms pulse (unlocked)
OCR1B (-90°) = 2000  →  1.0ms pulse (locked)
```

<br>

### Passkey Security

"Hashing with Salt" uses a salt (a random, unpredictable value) and encrypts it by putting the 
passkey and salt into a hashing algorithm. The salt is generated using the built-in rand() 
function seeded with millis(). As for our hashing algorithm, we use a XOR checksum which uses 
the bitwise XOR operator for each byte of the inputted key. With just the hash and salt values 
saved to EEPROM, it's extremely difficult to figure out the passkey especially without knowing 
the algorithm used.

1. User sets a PIN
2. A random salt is generated via `rand()` seeded with `millis()`
3. Salt + PIN are run through an XOR checksum hashing algorithm
4. Only the **hash** and **salt** are written to EEPROM — the PIN itself is never stored
5. On unlock attempt, the input is hashed with the stored salt and compared to the saved hash

## Build & Flashing (_How to Start_)

1. Wire the circuit just as you see in the schematic.
   (_currently the implemation of the standalone chip is a bit finicky, so for true functionality we
     reccommend using just the Arduino Uno, if you are uisng that disregard anything related to the standalone chip_)
2. Burn the Arduino Bootloader to the ATmega328p using Arduino Uno as an ISP (_if using Arduino Uno and not ATmega328p as a    standalone chip don't do this step_)
3. Upload the .iso file onto your Arduino Uno/ATmega328p standalone chip

## User Experience (_How to Use_)
| Action | Input |
|---|---|
|Wake from Sleep|Press any key (_preferably 0_)|
|Enter PIN|Press Number Keys|
|Delete previous digit|Press *|
|Confirm PIN|Press #|
|Lock (_when unlocked_)|Press #|
|Change PIN|Enter master key #2804 -> Press # -> enter new PIN -> Press #|

### Demo
Apologies for the poor quality on the demo video.

[Watch the demo](https://youtu.be/hFr4GKMdfvQ)
