# Desky — a desk companion pet

A small animated pet that lives on your desk. It has a face on one OLED, an
info panel on a second OLED, a head that moves, a voice made of beeps, and it
reads NFC cards that tell it what to do. Its main job is to nag you into
drinking water while you work.

Board: **NodeMCU ESP-12E (ESP8266)**. 3.3 V logic only.

---

## 1. Parts

| Part | Model | Notes |
|---|---|---|
| Board | NodeMCU ESP-12E (ESP8266) | 3.3 V logic only |
| Display A — face | SSD1306 OLED 0.96", 128x64, I2C | address **0x3C** |
| Display B — info | SSD1306 OLED 0.96", 128x64, I2C | address **0x3D** (jumper changed on the module) |
| NFC reader | RC522, SPI, 8 pins | **3.3 V only — 5 V will kill it** |
| Servo | SG90 micro servo | needs its own 5 V supply |
| Buzzer | Passive piezo buzzer | must be **passive**, not active |
| Touch | TTP223 capacitive touch module | output goes HIGH when touched |
| Cards | MIFARE Classic 1K | 4 cards: Work, Break, Sleep, Milk |

## 2. Signal wiring — 9 wires

| NodeMCU | GPIO | Goes to | Wire colour |
|---|---|---|---|
| D0 | GPIO16 | TTP223 → I/O (signal out) | purple |
| D1 | GPIO5  | **Both OLEDs** → SCL | yellow |
| D2 | GPIO4  | **Both OLEDs** → SDA | green |
| D3 | GPIO0  | Buzzer → + (positive) | orange |
| D4 | GPIO2  | RC522 → SDA (chip select) | blue |
| D5 | GPIO14 | RC522 → SCK | white |
| D6 | GPIO12 | RC522 → MISO | grey |
| D7 | GPIO13 | RC522 → MOSI | pink |
| D8 | GPIO15 | SG90 servo → signal | orange (servo's own) |

Both displays sit on the same two I2C wires. They are told apart by
**address, not by pins** — the second display costs no extra GPIO.

## 3. Power wiring — use the breadboard rails

**Red rail = 3.3 V**, fed from the NodeMCU `3V3` pin with a red wire. Then,
with red wires: OLED A → VCC, OLED B → VCC, RC522 → 3.3V, RC522 → RST (tie
RST straight to 3.3 V, this saves a GPIO), TTP223 → VCC.

**Black rail = GND**, fed from the NodeMCU `GND` pin with a black wire. Then,
with black wires: OLED A → GND, OLED B → GND, RC522 → GND, TTP223 → GND,
Buzzer → −, Servo → brown wire, 5 V adapter → negative.

**Second rail = 5 V**, from a separate **5 V 2 A adapter** — *not* from the
NodeMCU. Servo → red wire only. Put a 1000 µF capacitor across this rail.

> The 5 V adapter ground **must** join the NodeMCU ground. Without a shared
> ground the servo will not move.

> RC522 **IRQ** stays empty. Do not connect it.

## 4. Module pin order — read the printed labels, never count positions

- **OLED (4 pins)** — usually `GND VCC SCL SDA`, some are `VCC GND SCL SDA`.
  Swapping VCC and GND destroys it.
- **RC522 (8 pins)** — `SDA SCK MOSI MISO IRQ GND RST 3.3V`
- **TTP223 (3 pins)** — usually `VCC GND I/O`
- **SG90 servo** — brown = GND, red = 5 V, orange = signal
- **Buzzer** — the longer leg, or the `+` mark, is positive

## 5. The two displays — the core layout

Physically, **A sits on top and B below it**, like a head with a chest panel.

**Display A (0x3C) is the face.** It shows *only* the face, always. Never
text, numbers, a clock or a menu — not even for a second. This is enforced in
code: `faces.cpp` owns display A and has no text-drawing function at all.

**Display B (0x3D) is the info panel.** All numbers and text live here.

| Mode | Display B shows |
|---|---|
| IDLE | time, date, weather |
| WORK | session timer (large font), glasses of water, minutes until next water reminder |
| BREAK | break countdown |
| SLEEP | blank, or dim time only |
| EATING | nothing special, keeps previous content |
| Card tapped | the mode name for 2 s, then back to normal content |
| Session end | summary for 5 s — minutes worked, glasses of water |

Rules the code follows:

- Two `U8g2` objects, one per address. Both addresses are `#define`s in
  `config.h` (`OLED_FACE_ADDR`, `OLED_INFO_ADDR`).
- `Wire.setClock(400000)` — I2C fast mode. The 100 kHz default is too slow
  for two screens.
- **Display B is only redrawn when its content actually changed.** The last
  drawn values are stored and compared first. Redrawing B every frame would
  destroy the frame rate.
- Display A redraws every frame (every 30 ms). Display B redraws at most
  once per second.
- An **I2C scan runs in `setup()`** and logs what it found to serial. If
  display B is missing, Desky still runs fully on display A alone — it never
  crashes or hangs because B is absent.

## 6. Boot-strap pins — read this if the board will not boot

| Pin | Must be at power-up |
|---|---|
| GPIO0 (D3, buzzer) | HIGH — LOW means flash mode |
| GPIO2 (D4, RC522 SS) | HIGH |
| GPIO15 (D8, servo) | LOW |

So the code never pulls GPIO0 or GPIO2 low in `setup()`, and never calls
`tone()` or `SPI.begin()` before the board is past its boot sequence.

---

## 7. Libraries

| Library | Used for | Install |
|---|---|---|
| U8g2 | both OLEDs, full buffer mode | `arduino-cli lib install U8g2` |
| MFRC522 | RC522 NFC reader | `arduino-cli lib install MFRC522` |
| Servo | SG90 head | built into the ESP8266 core |
| ESP8266WiFi + `configTime` | Wi-Fi and NTP clock | built into the ESP8266 core |

## 8. Files

```
desky/
  desky.ino        setup, loop, mode state machine, I2C scan
  config.h         pins, addresses, wifi creds, card UIDs, timings
  faces.h/.cpp     face drawing + animation engine      (display A)
  infopanel.h/.cpp info screen + change detection       (display B, step 2)
  gestures.h/.cpp  servo moves: nod, shake, tilt, droop, wiggle  (step 5)
  sounds.h/.cpp    buzzer tunes                                  (step 4)
  cards.h/.cpp     NFC read, UID to card-name mapping            (step 6)
tools/
  i2c_scan/i2c_scan.ino  standalone: prints every I2C address found
  read_uid/read_uid.ino  standalone: prints card UIDs only
```

`config.h` is the only file containing a pin number, an I2C address or a
timing value. When this project moves to an ESP32, that is the only file
that should need changing.

## 9. Adding a new card UID

1. Flash `tools/read_uid/read_uid.ino` to the board.
2. Open the serial monitor at **115200** baud.
3. Tap a card. You will see a line like:

   ```
   UID: "A3 1F 7C 02"   <- paste this into config.h   (4 bytes)
   ```

4. Copy that text into the matching line in `desky/config.h`:

   ```c
   #define CARD_UID_WORK       "A3 1F 7C 02"
   #define CARD_UID_BREAK      "11 22 33 44"
   #define CARD_UID_SLEEP      "55 66 77 88"
   #define CARD_UID_MILK       "99 AA BB CC"
   ```

   Upper case, one space between bytes, exactly as printed.

5. Re-flash `desky`. You can also skip the helper sketch: the main sketch
   prints any UID it does not recognise to serial in the same format.

---

## 10. Building

```sh
arduino-cli core install esp8266:esp8266
arduino-cli lib install U8g2
arduino-cli lib install MFRC522

arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 desky
arduino-cli upload  --fqbn esp8266:esp8266:nodemcuv2 -p /dev/cu.usbserial-XXXX desky
arduino-cli monitor -p /dev/cu.usbserial-XXXX -c baudrate=115200
```

### Apple Silicon note

The ESP8266 core ships only Intel (x86_64) build tools — there is no arm64
macOS toolchain in its package index. On an M-series Mac you need Rosetta 2:

```sh
softwareupdate --install-rosetta --agree-to-license
```

This repo's setup also replaces the core's bundled Intel `python3` helper
with a wrapper around the system `python3`, at
`~/Library/Arduino15/packages/esp8266/tools/python3/3.7.2-post1/python3`.
The original binary is kept beside it as `python3.x86_64.orig`.

---

## 11. Build order

Each step is finished and compiling before the next one starts.

- [x] **1. I2C scan + display A: the full face and animation engine.**
      Inputs faked over serial.
- [ ] 2. Display B, the info panel, with the change-detection redraw rule.
      Confirm display A's frame rate did not drop.
- [ ] 3. Touch sensor and all touch reactions
- [ ] 4. Buzzer and all sounds
- [ ] 5. Servo and all gestures, with the `detach()` rule
- [ ] 6. NFC reading, the card table, mode switching
- [ ] 7. Wi-Fi, NTP, night behaviour
- [ ] 8. Water reminder, session tracking, summary screen

## 12. Testing step 1 over serial

Only display A needs to be connected. Open the serial monitor at 115200 baud
and type `help`. Anything that would appear on display B is printed to serial
tagged `[infoB]` until step 2 exists.

| Type | Commands |
|---|---|
| Faces | `calm happy loved surprised confused sad sleepy sleeping focus thirsty celebrate eating` |
| Cards | `work` `break` `sleep` `milk` `unknown` `idle` — `work` again ends the session |
| Touch | `tap` `hold` `annoy` `drank` `clock` |
| Gaze | `left` `right` `centre` `free` |
| Other | `blink` `night` `day` `dim` `bright` `tour` `stop` `scan` `help` |

`tour` walks through all twelve expressions, 2 seconds each — the quickest
way to check the whole face at once. `scan` re-runs the I2C scan.

At boot you should see:

```
[i2c] scanning the bus...
[i2c]   found 0x3C  display A (face)
[i2c]   found 0x3D  display B (info)
```

and once per second:

```
[dbg] mode=WORK face=FOCUS fpsA=33 heap=43112 water=0 dispB=yes worked=3min
```

---

## 13. Design rules this code follows

- **No `delay()` anywhere.** Every timer is a `millis()` deadline check. The
  only legal wait is `yield()`, which hands time to the Wi-Fi stack. (The two
  throwaway sketches in `tools/` do use `delay()` — they are not Desky.)
- **20+ FPS on display A.** U8g2 runs in full buffer mode at a 400 kHz I2C
  clock, and the frame is pushed in one burst every 30 ms.
- **Display B only redraws on change**, at most once a second.
- **The NFC reader is polled every 200 ms**, never every loop.
- **`servo.detach()` whenever the head is still.** The Servo library eats
  timer cycles and makes the animation stutter if it stays attached.
- **Wi-Fi never blocks.** If it cannot connect, Desky works fully, just
  without the clock.
- **Display A never shows text.** The face module has no text function.
