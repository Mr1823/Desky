/*
 * touch.cpp - TTP223 capacitive touch sensor.  See touch.h for the overview.
 *
 * Reading guide:
 *   1. Debounce   - turns the noisy digital pin into clean down / up edges
 *   2. Tap counter - counts fast taps and waits a gap before deciding
 *   3. Hold timer  - fires TOUCH_ACK at 300 ms, TOUCH_HOLD at 2 s
 *   4. touchUpdate() - the main state machine, returns one event per call
 *
 * No delay(), no blocking. The TTP223 is read at most once per loop.
 */
#include "touch.h"
#include "config.h"

// =====================================================================
// 1. DEBOUNCE
// =====================================================================
// The raw pin might bounce for a few milliseconds when the finger lands or
// lifts. We ignore any change that is shorter than TOUCH_DEBOUNCE_MS.
static bool     rawDown      = false;   // last raw reading
static bool     stableDown   = false;   // debounced state
static uint32_t lastEdgeAt   = 0;       // when the raw state last changed

static bool debounce(uint32_t now) {
  bool pin = (digitalRead(PIN_TOUCH) == HIGH);

  if (pin != rawDown) {
    rawDown    = pin;
    lastEdgeAt = now;
  }

  // The reading has been steady for long enough: accept it.
  if (rawDown != stableDown && (now - lastEdgeAt) >= TOUCH_DEBOUNCE_MS) {
    stableDown = rawDown;
  }
  return stableDown;
}

// =====================================================================
// 2. STATE
// =====================================================================
static uint8_t  tapCount       = 0;        // taps counted so far in this burst
static uint32_t lastTapUpAt    = 0;        // when the last tap ended (finger lifted)
static uint32_t downAt         = 0;        // when the current press started
static bool     wasDown        = false;    // debounced state last loop
static bool     ackFired       = false;    // TOUCH_ACK already reported this press
static bool     holdFired      = false;    // TOUCH_HOLD already reported this press
static bool     waitingForGap  = false;    // we have taps and are waiting out the multi-gap

// =====================================================================
// 3. PUBLIC
// =====================================================================
void touchBegin() {
  pinMode(PIN_TOUCH, INPUT);
  // GPIO16 has no internal pull-down that the Arduino API can set, but the
  // TTP223 output is push-pull, so INPUT is fine.
}

bool touchIsDown() { return stableDown; }

const char *touchEventName(TouchEvent e) {
  switch (e) {
    case TOUCH_TAP:     return "TAP";
    case TOUCH_DOUBLE:  return "DOUBLE";
    case TOUCH_ANNOY:   return "ANNOY";
    case TOUCH_ACK:     return "ACK";
    case TOUCH_HOLD:    return "HOLD";
    case TOUCH_RELEASE: return "RELEASE";
    default:            return "NONE";
  }
}

// =====================================================================
// 4. touchUpdate() - the state machine
// =====================================================================
// Returns at most one event per call. Priority:
//   - edges (down / up) are handled first
//   - while held: ACK at 300 ms, HOLD at 2 s
//   - after release: wait the multi-gap, then decide tap / double / annoy
TouchEvent touchUpdate(uint32_t now) {
  bool down = debounce(now);

  // ---- falling edge: finger just landed ---------------------------------
  if (down && !wasDown) {
    wasDown   = true;
    downAt    = now;
    ackFired  = false;
    holdFired = false;
    return TOUCH_NONE;           // wait and see what this press becomes
  }

  // ---- rising edge: finger just lifted ----------------------------------
  if (!down && wasDown) {
    wasDown = false;

    // Was it a hold?  The hold ends here.
    if (holdFired) {
      holdFired = false;
      tapCount  = 0;
      return TOUCH_RELEASE;
    }

    // Was the press short enough to count as a tap?
    if ((now - downAt) <= TOUCH_TAP_MAX_MS) {
      tapCount++;
      lastTapUpAt   = now;
      waitingForGap = true;

      // If we already have 5 taps, fire immediately — no need to wait.
      if (tapCount >= TOUCH_ANNOY_TAPS) {
        tapCount      = 0;
        waitingForGap = false;
        return TOUCH_ANNOY;
      }
    } else {
      // It was not a tap (300 ms < duration < 2 s) and not a hold.
      // The ack may already have fired; either way, the press is done.
      tapCount      = 0;
      waitingForGap = false;
    }
    return TOUCH_NONE;
  }

  // ---- finger is down: check for ack / hold thresholds ------------------
  if (down) {
    uint32_t held = now - downAt;

    if (!ackFired && held >= TOUCH_ACK_MS) {
      ackFired = true;
      // Reset tap count: a long press is not part of a tap burst.
      tapCount      = 0;
      waitingForGap = false;
      return TOUCH_ACK;
    }

    if (!holdFired && held >= TOUCH_HOLD_MS) {
      holdFired = true;
      return TOUCH_HOLD;
    }

    return TOUCH_NONE;
  }

  // ---- finger is up: is the multi-tap gap over? -------------------------
  if (waitingForGap && (now - lastTapUpAt) >= TOUCH_MULTI_GAP_MS) {
    waitingForGap = false;
    uint8_t n     = tapCount;
    tapCount      = 0;

    if (n >= TOUCH_ANNOY_TAPS)  return TOUCH_ANNOY;
    if (n >= TOUCH_DOUBLE_TAPS) return TOUCH_DOUBLE;
    if (n == 1)                 return TOUCH_TAP;
    // n == 0 should not happen here, but guard anyway.
  }

  return TOUCH_NONE;
}

