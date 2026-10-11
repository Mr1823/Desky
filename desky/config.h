/*
 * config.h - EVERYTHING that depends on the board lives here.
 *
 * Rule for this project: no other file may contain a pin number or a
 * timing value. When Desky is moved to an ESP32 later, only this file
 * should need editing.
 */
#ifndef CONFIG_H
#define CONFIG_H

// =====================================================================
// 1. PINS  (NodeMCU ESP-12E / ESP8266)
// =====================================================================
// NodeMCU | GPIO | goes to                      | wire colour
// --------+------+------------------------------+-------------
//   D0    |  16  | TTP223  I/O                  | purple
//   D1    |   5  | OLED    SCL                  | yellow
//   D2    |   4  | OLED    SDA                  | green
//   D3    |   0  | Buzzer  +                    | orange
//   D4    |   2  | RC522   SDA (chip select)    | blue
//   D5    |  14  | RC522   SCK                  | white
//   D6    |  12  | RC522   MISO                 | grey
//   D7    |  13  | RC522   MOSI                 | pink
//   D8    |  15  | SG90    signal               | orange (servo's own)

#define PIN_TOUCH         16     // D0  TTP223, reads HIGH while touched
#define PIN_SCL            5     // D1  OLED
#define PIN_SDA            4     // D2  OLED
#define PIN_BUZZER         0     // D3  passive piezo, driven with tone()
#define PIN_RC522_SS       2     // D4  RC522 chip select
#define PIN_SCK           14     // D5  RC522
#define PIN_MISO          12     // D6  RC522
#define PIN_MOSI          13     // D7  RC522
#define PIN_SERVO         15     // D8  SG90, own 5V supply, shared ground
#define PIN_RC522_RST    255     // RST is tied to 3.3V in hardware, no GPIO

// Both OLEDs sit on the SAME two I2C wires (D1/D2). They are told apart by
// address only - the second display costs no extra GPIO. Display B's address
// is changed with the solder jumper on the back of the module.
#define OLED_FACE_ADDR     0x3C   // display A: the face, and nothing else
#define OLED_INFO_ADDR     0x3D   // display B: all text and numbers
#define OLED_I2C_CLOCK_HZ  400000UL   // fast mode. The 100 kHz default is far
                                      // too slow to drive two screens.

/* ---------------------------------------------------------------------
 * BOOT-STRAP PIN WARNING (read before changing any pin above)
 *
 *   GPIO0  (D3) must be HIGH at power-up   - LOW means flash mode
 *   GPIO2  (D4) must be HIGH at power-up
 *   GPIO15 (D8) must be LOW  at power-up
 *
 * So: never pull GPIO0 or GPIO2 low in setup(). Set the buzzer and the
 * RC522 chip select up as HIGH-idle outputs only after the board has
 * booted, and never call tone() or SPI.begin() before that. If the board
 * refuses to boot, one of these three pins is being driven the wrong way.
 * GPIO15 already idles low, which is what the servo signal line wants.
 *
 * The 5V adapter ground and the NodeMCU ground must be joined, or the
 * servo will not move. RC522 IRQ stays unconnected.
 * ------------------------------------------------------------------- */

// =====================================================================
// 2. WI-FI + TIME
// =====================================================================
#define WIFI_SSID           "YOUR_WIFI_NAME"
#define WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"
#define WIFI_RETRY_MS       30000UL  // how often to retry a failed connect
#define NTP_SERVER_1        "pool.ntp.org"
#define NTP_SERVER_2        "time.google.com"
#define TZ_OFFSET_SECONDS   19800    // IST = UTC +5:30 = 5*3600 + 30*60
#define TZ_DST_SECONDS      0        // India has no daylight saving
#define NIGHT_START_HOUR    22       // after 22:00 Desky looks sleepy

// =====================================================================
// 3. NFC CARD TABLE  -  this is the part you will edit most
// =====================================================================
// Tap an unknown card and the serial monitor prints its UID in exactly
// this format. Copy that text between the quotes below. Upper case,
// single space between bytes.
#define CARD_UID_WORK       "DE AD BE EF"
#define CARD_UID_BREAK      "11 22 33 44"
#define CARD_UID_SLEEP      "55 66 77 88"
#define CARD_UID_MILK       "99 AA BB CC"

#define NFC_POLL_MS         200      // never poll the RC522 faster than this
#define CARD_REPEAT_LOCK_MS 1500     // ignore the same card if re-read at once

// =====================================================================
// 4. DISPLAY / ANIMATION TIMING
// =====================================================================
#define FRAME_INTERVAL_MS   30       // display A redraws this often (~33 FPS)
#define INFO_MIN_REDRAW_MS  1000     // display B redraws at most once a second,
                                     // and only if its content actually changed
#define INFO_MODE_FLASH_MS  2000     // display B shows the mode name this long
                                     // after a card is tapped
#define OLED_CONTRAST_FULL  255      // normal brightness
#define OLED_CONTRAST_DIM   8        // sleep mode brightness

// Display B is pushed to the panel in slices so display A never stalls:
// one frame is 8 pages, and only this many are sent per frame of display A.
#define INFO_PAGES_PER_FRAME 2

// Blinking. The interval is re-randomised after every blink so it never
// looks mechanical.
#define BLINK_MIN_MS        3000
#define BLINK_MAX_MS        5000
#define BLINK_CLOSE_MS      50       // eyes closing
#define BLINK_HOLD_MS       50       // eyes held shut
#define BLINK_OPEN_MS       50       // eyes opening  (total = 150 ms)
#define BLINK_DOUBLE_CHANCE 25       // % chance a blink becomes a double blink
#define BLINK_DOUBLE_GAP_MS 180

// Idle glances left and right
#define LOOK_MIN_MS         8000
#define LOOK_MAX_MS         15000
#define LOOK_HOLD_MS        1200
#define GAZE_SHIFT_PX       6        // how far the eyes slide sideways
#define GAZE_SPEED_PX       1        // pixels moved per frame (easing)

// Slow 1-pixel up/down drift of the whole face, so it looks alive
#define BREATH_PERIOD_MS    4000
#define BREATH_AMPLITUDE_PX 1

// BREAK mode is the same animation, just livelier
#define LIVELY_BLINK_MIN_MS 1500
#define LIVELY_BLINK_MAX_MS 3000
#define LIVELY_LOOK_MIN_MS  2500
#define LIVELY_LOOK_MAX_MS  5000
#define LIVELY_BOUNCE_PX    3
#define LIVELY_BOUNCE_MS    900

// Night mode multiplies every animation period by this, so Desky slows down
#define NIGHT_SLOWDOWN      2

// Periods of the small looping animations on the face (display A)
#define CONFUSED_BOB_MS     1200     // the "?" floats up and down
#define SLEEPY_Z_PERIOD_MS  2000     // the single "z" of SLEEPY drifts up
#define ZZZ_PERIOD_MS       2400     // the three Zs of SLEEPING chase each other
#define THIRSTY_BOB_MS      900      // water droplet bobs
#define SPARKLE_PERIOD_MS   1000     // CELEBRATE sparkles pulse
#define SPARKLE_STAGGER_MS  250      // each sparkle starts this much later
#define MILK_TILT_PERIOD_MS 1300     // EATING: the milk glass tilts
#define CHEW_PERIOD_MS      500      // EATING: mouth opens and shuts

// Serial test tour (step 1 helper): how long each expression is shown
#define TOUR_STEP_MS        2000

// How long a one-off reaction face is shown before falling back to the
// face that belongs to the current mode
#define REACTION_HOLD_MS    3000

// =====================================================================
// 5. MODE / FEATURE TIMING
// =====================================================================
#define BREAK_DURATION_MS   (5UL * 60UL * 1000UL)   // 5 minutes
#define EATING_DURATION_MS  4000UL                  // milk card animation
#define SUMMARY_SCREEN_MS   5000UL                  // end-of-work summary
#define CLOCK_SCREEN_MS     3000UL                  // double-tap clock peek

// Water reminder (WORK mode only)
#define WATER_INTERVAL_MS   (15UL * 60UL * 1000UL)  // first nudge after 15 min
#define WATER_NAG_MS        (60UL * 1000UL)         // then every 60 s
#define WATER_NAG_MAX       5                       // give up after 5 tries

// =====================================================================
// 6. TOUCH SENSOR TIMING
// =====================================================================
#define TOUCH_DEBOUNCE_MS   40
#define TOUCH_HOLD_MS       2000     // "lean into the hand" hold
#define TOUCH_ACK_MS        300      // hold this long = "I drank the water"
#define TOUCH_TAP_MAX_MS    400      // longer than this is not a tap
#define TOUCH_MULTI_GAP_MS  450      // max gap between taps in a burst
#define TOUCH_DOUBLE_TAPS   2        // double tap -> show clock
#define TOUCH_ANNOY_TAPS    5        // 5 fast taps -> annoyed

// =====================================================================
// 7. SERVO (head) GEOMETRY
// =====================================================================
#define SERVO_CENTER_DEG    90
#define SERVO_LEFT_DEG      55
#define SERVO_RIGHT_DEG     125
#define SERVO_STEP_MS       15       // one step of movement every 15 ms
#define SERVO_STEP_DEG      3        // degrees per step
#define SERVO_SETTLE_MS     200      // wait this long, then detach()

// =====================================================================
// 8. DEBUG
// =====================================================================
#define SERIAL_BAUD         115200
#define DEBUG_PRINT_MS      1000     // mode + FPS + heap once per second

#endif // CONFIG_H
