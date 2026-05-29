Chopper C1-10P

Arduino code for a Chopper droid robot thing I'm building. Based on the C1-10P from Star Wars Rebles but it's mostly custom 3D printed parts

What's in here

- `Code/chopper_arduino.ino` — main brain on a Yahboom Roboduino + Arduino UNO. Runs servos, motors, sounds, mode switching
- `chopper_yahboom_ports.txt` — pin/channel map so I don't forget what plugs into where.

There's a second ESP32 board for the camera and face tracking but that code isn't in this repo yet. The two boards talk over serial.

## Electronics

- ESP32-S3 dev board w/ camera
- Arduino UNO
- Yahboom expansion board w/ motor drivers intigrated into the board along with servo ports
- Logic level shifter
- 1k resistor
- DFPlayer Mini
- Micro SD card
- Speaker
- Ultrasonic sensor
- 6 servo motors
- 3 DC motors
- 6 AA battery pack

## Modes

**Upright** — standing around, legs down  
**Wheel** — legs fold, center wheel drops, drives like a little tank

ESP32 sends commands like `MOVE_FWD`, `TURN_LEFT`, `FACE_LEFT` etc and the Arduino handles the rest.

## Notes

Still a work in progress. Angles and timings will probably change once everything is actually mounted. If something doesn't work it's probably a wiring issue or I forgot to update the port map.
