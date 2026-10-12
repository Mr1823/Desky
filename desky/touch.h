/*
 * touch.h - TTP223 capacitive touch sensor on PIN_TOUCH (D0, GPIO16).
 *
 * The sensor reads HIGH while the finger is on the pad.
 *
 * This module debounces the raw signal, counts taps, measures holds, and
 * reports events back to the main sketch:
 *
 *   TOUCH_NONE     - nothing happened (returned every loop when it is quiet)
 *   TOUCH_TAP      - a single tap, after waiting long enough to tell it from
 *                    a double tap (~250 ms gap)
 *   TOUCH_DOUBLE   - two taps in quick succession
 *   TOUCH_ANNOY    - five fast taps
 *   TOUCH_ACK      - finger held for 300 ms (the "I drank" water ack).
 *                    This fires even if the hold continues toward 2 s.
 *   TOUCH_HOLD     - finger held for 2 s (eyes shut, lean into hand)
 *   TOUCH_RELEASE  - finger lifted after a hold
 *
 * The ack event (TOUCH_ACK) fires once as soon as the hold reaches 300 ms.
 * If the finger stays on, TOUCH_HOLD fires later at 2 s. The main sketch
 * decides what to do: during a water reminder, the ack wins over everything
 * else; at other times it is ignored and only the tap / double / hold matter.
 *
 * Nothing in here blocks. touchUpdate() reads the pin, advances the state
 * machine, and returns one TouchEvent.
 */
#ifndef TOUCH_H
#define TOUCH_H

#include <Arduino.h>

enum TouchEvent : uint8_t {
  TOUCH_NONE = 0,
  TOUCH_TAP,
  TOUCH_DOUBLE,
  TOUCH_ANNOY,
  TOUCH_ACK,
  TOUCH_HOLD,
  TOUCH_RELEASE
};

void       touchBegin();                   // call once in setup()
TouchEvent touchUpdate(uint32_t now);      // call every loop, returns one event
bool       touchIsDown();                  // true while the finger is on the pad
const char *touchEventName(TouchEvent e);  // for serial debug

#endif // TOUCH_H

