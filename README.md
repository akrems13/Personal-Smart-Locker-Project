# Smart Locker Project

A secure "battery-powered" smart locker system built on a standalone ATmega328p microcontroller. 
Features numeric-locking, encrypted password storing, idle/sleep mode for power efficency, and 
low battery alerts. This is all being made without the use of external libraries.

***

- Features
   - SG90 Servo motor control, made with a custom PWM servo library, using Timer1 interrupts
   - 12-key numeric keypad, made with a resistor ladder
   - Finite State Machine (_FSM_) manages all system states, such as, _LOCKED, UNLOCKED, SLEEP, PASSKEY CHECKING, etc._
   - Power consumption efficiency through our sleep mode, works using SMCR registers and wakes on INT0 hardware interrupts
   - No hard-coded passkeys, password encryption with a simple XOR checksum with random (_rand()_) salt
   - Overcurrent detection/protection, if rotor is jammed, program will detect that via shunt resistor
   - LED feedback
   - Low battery alerting, LEDs flash when battery falls below threshold
     
***
