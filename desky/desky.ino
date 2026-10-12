/*
 * Desky - a desk companion pet on a NodeMCU ESP-12E (ESP8266).
 *
 * ==== BUILD STEPS 1-3 of 8 ====
 * I2C scan, display A (the face), display B (the info panel), touch sensor.
 *
 * Desky has two OLEDs on the same two I2C wires, told apart by address:
 *   display A, 0x3C - the FACE. Only ever the face. No text, no numbers.
 *   display B, 0x3D - the INFO PANEL. Every word and digit lives there.
 *
 * Display B lives in infopanel.cpp. If it is not connected, Desky runs on
 * display A alone. Nothing but the face ever reaches display A.
 *
 * The TTP223 touch sensor is live (touch.cpp): single tap, double tap,
 * 5-tap annoy, 2 s hold, 300 ms ack (water reminder priority rule).
 * Serial commands remain as test overrides.
 *
 * The buzzer, servo, NFC reader and Wi-Fi are not wired up in code yet,
 * so those inputs are still faked over serial. Open the serial monitor at
 * 115200 baud and type "help".
 *
 * Golden rules (the long version is in config.h):
 *   - no delay() anywhere, every timer is a millis() deadline
 *   - nothing in loop() may block, the animation must never freeze
 */
#include <Wire.h>
#include "config.h"
#include "faces.h"
#include "infopanel.h"
#include "touch.h"

// =====================================================================
// MODES
// =====================================================================
enum Mode { MODE_IDLE, MODE_WORK, MODE_BREAK, MODE_SLEEP, MODE_EATING };

static const char *const MODE_NAMES[] = {"IDLE", "WORK", "BREAK", "SLEEP", "EATING"};

static Mode     mode        = MODE_IDLE;
static Mode     modeBefore  = MODE_IDLE;   // where EATING / BREAK return to
static uint32_t modeStarted = 0;

// Session counters, kept for the summary on display B (step 8).
static uint32_t workStartedAt  = 0;
static uint16_t glassesOfWater = 0;
static uint16_t happiness      = 0;
static uint32_t lastWaterAt    = 0;   // start of the current water countdown

// MILK interrupts a mode for 4 s. These remember what it interrupted and
// when that mode started, so BREAK's 5 minute clock is not restarted.
static Mode     eatReturnMode  = MODE_IDLE;
static uint32_t eatReturnStart = 0;

// Which displays actually answered on the bus. Desky must run fully even
// if display B is missing, so this is a flag and never an error.
static bool faceDisplayFound = false;
static bool infoDisplayFound = false;

// Night mode is faked with the "night" command until step 7 brings NTP in.
static bool fakeNight = false;

// Fake clock + weather so display B can be tested before Wi-Fi exists.
// Step 7 replaces these with real NTP time.
static bool     fakeTimeOn    = false;
static uint16_t fakeBaseMin   = 0;     // minutes after midnight when "time" was typed
static uint32_t fakeBaseMs    = 0;
static bool     fakeWeatherOn = false;
static int16_t  fakeTempC     = 0;

// FPS / debug
static uint32_t lastFrameAt = 0;
static uint32_t lastDebugAt = 0;
static uint16_t frameCount  = 0;
static uint16_t fps         = 0;

// The "tour" demo walks through every expression, 2 s each.
static bool     tourRunning = false;
static uint8_t  tourIndex   = 0;
static uint32_t tourNextAt  = 0;

// Touch-hold: eyes stay shut while the finger is on the pad
static bool     holdEyesActive = false;

// Water reminder active flag. The real logic arrives in step 8; this is here
// so the touch handler can check it when deciding whether TOUCH_ACK means
// "I drank the water" (priority rule) or should be ignored.
static bool     waterNagging = false;

// =====================================================================
// I2C SCAN
// =====================================================================
// Walks every valid 7-bit address and reports who answers. Run once in
// setup() so a wiring mistake shows up as a clear serial line instead of
// as a mysteriously blank screen.
static void scanI2C() {
  Serial.println(F("[i2c] scanning the bus..."));
  uint8_t found = 0;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {        // 0 means the device replied
      found++;
      Serial.print(F("[i2c]   found 0x"));
      if (addr < 16) Serial.print('0');
      Serial.print(addr, HEX);
      if      (addr == OLED_FACE_ADDR) { Serial.print(F("  display A (face)")); faceDisplayFound = true; }
      else if (addr == OLED_INFO_ADDR) { Serial.print(F("  display B (info)")); infoDisplayFound = true; }
      else                               Serial.print(F("  unknown device"));
      Serial.println();
    }
    yield();                                  // never starve the Wi-Fi stack
  }

  if (found == 0)
    Serial.println(F("[i2c] NOTHING found. Check SDA(D2), SCL(D1), 3.3V and GND."));
  if (!faceDisplayFound)
    Serial.println(F("[i2c] WARNING display A (0x3C) is missing - no face."));
  if (!infoDisplayFound)
    Serial.println(F("[i2c] display B (0x3D) absent - running on display A alone."));
}

// =====================================================================
// FEEDING DISPLAY B
// =====================================================================
// While EATING, display B keeps showing whatever the interrupted mode
// shows, so these two look straight through EATING to the real mode.
static Mode     underlyingMode()  { return mode == MODE_EATING ? eatReturnMode  : mode; }
static uint32_t underlyingStart() { return mode == MODE_EATING ? eatReturnStart : modeStarted; }

// Collect "what is true right now" for infopanel.cpp. It decides what to
// draw and whether anything changed - this function only reports facts.
static void fillInfoInput(uint32_t now, InfoInput &in) {
  memset(&in, 0, sizeof(in));

  switch (underlyingMode()) {
    case MODE_WORK:  in.mode = INFO_WORK;  break;
    case MODE_BREAK: in.mode = INFO_BREAK; break;
    case MODE_SLEEP: in.mode = INFO_SLEEP; break;
    default:         in.mode = INFO_IDLE;  break;
  }

  // clock (fake until step 7)
  in.timeValid = fakeTimeOn;
  if (fakeTimeOn) {
    uint32_t minutes = (fakeBaseMin + (now - fakeBaseMs) / 60000UL) % 1440UL;
    in.hour = minutes / 60;  in.minute = minutes % 60;
    in.wday = 1;  in.mday = 22;  in.month = 9;      // a fixed example date: Mon, 22 Sep
  }
  in.weatherValid = fakeWeatherOn;
  in.tempC        = fakeTempC;

  // WORK
  in.workSeconds = (now - workStartedAt) / 1000UL;
  in.glasses     = glassesOfWater;
  uint32_t sinceWater = now - lastWaterAt;
  in.minsToWater = (sinceWater >= WATER_INTERVAL_MS) ? 0
                 : (uint16_t)((WATER_INTERVAL_MS - sinceWater + 59999UL) / 60000UL);

  // BREAK
  uint32_t sinceBreak = now - underlyingStart();
  in.breakSecondsLeft = (sinceBreak >= BREAK_DURATION_MS) ? 0
                      : (BREAK_DURATION_MS - sinceBreak + 999UL) / 1000UL;
}

// =====================================================================
// MODE SWITCHING
// =====================================================================

// The face that belongs to a mode when nothing else is reacting.
static Expression faceForMode(Mode m) {
  switch (m) {
    case MODE_WORK:   return EXP_FOCUS;
    case MODE_BREAK:  return EXP_HAPPY;
    case MODE_SLEEP:  return EXP_SLEEPING;
    case MODE_EATING: return EXP_EATING;
    case MODE_IDLE:
    default:          return fakeNight ? EXP_SLEEPY : EXP_CALM;
  }
}

static void enterMode(Mode m, bool announce) {
  if (m != MODE_EATING && m != MODE_BREAK) modeBefore = m;
  mode        = m;
  modeStarted = millis();

  facesSetBase(faceForMode(m));
  facesSetLively(m == MODE_BREAK);          // BREAK blinks and glances more
  facesSetDim(m == MODE_SLEEP);             // SLEEP dims the panel
  facesSetSlow(fakeNight || m == MODE_SLEEP);

  Serial.print(F("[mode] -> "));
  Serial.println(MODE_NAMES[m]);
  if (announce) infoFlashMode(MODE_NAMES[m]);   // display B shows the name for 2 s
}

static void setMode(Mode m) { enterMode(m, true); }

// Each card tap, faked over serial for now. The real reader arrives in
// step 6, but the behaviour here is already final.
static void onWorkCard() {
  if (mode == MODE_WORK) {
    // Tapping WORK again ends the session. The summary goes on display B;
    // the face celebrates on display A.
    uint32_t minutes = (millis() - workStartedAt) / 60000UL;
    uint16_t glasses = glassesOfWater;
    glassesOfWater = 0;
    setMode(MODE_IDLE);
    infoShowSummary(minutes, glasses);   // after setMode, so it replaces the mode-name flash
    facesReact(EXP_CELEBRATE);
  } else {
    workStartedAt  = millis();
    lastWaterAt    = workStartedAt;
    glassesOfWater = 0;
    setMode(MODE_WORK);
  }
}

static void onBreakCard() { setMode(MODE_BREAK); }
static void onSleepCard() { setMode(MODE_SLEEP); }

static void onMilkCard() {
  // MILK does not change the mode. It plays an animation, then hands
  // control back to whatever Desky was doing before.
  happiness++;
  if (mode != MODE_EATING) {          // a second milk card must not overwrite what we remember
    eatReturnMode  = mode;
    eatReturnStart = modeStarted;
  }
  mode        = MODE_EATING;
  modeStarted = millis();
  facesSetBase(EXP_EATING);
  Serial.print(F("[mode] -> EATING  happiness="));
  Serial.println(happiness);
}

static void onUnknownCard() {
  facesReact(EXP_CONFUSED);
  Serial.println(F("[card] unknown UID -> CONFUSED (head shake + 2 low beeps: steps 4-5)"));
}

// Time-based mode exits. No delay(), just deadline checks.
static void updateModeTimers(uint32_t now) {
  if (mode == MODE_EATING && now - modeStarted >= EATING_DURATION_MS) {
    enterMode(eatReturnMode, false);   // quiet: EATING changes nothing on display B
    modeStarted = eatReturnStart;      // BREAK keeps its original 5 minute clock
    facesReact(EXP_HAPPY);
  }
  else if (mode == MODE_BREAK && now - modeStarted >= BREAK_DURATION_MS) {
    // Break is over: look at the user, then go back to what we were doing.
    Serial.println(F("[break] 5 min up - nudging (beep comes in step 4)"));
    setMode(modeBefore == MODE_WORK ? MODE_WORK : MODE_IDLE);
    facesReact(EXP_SURPRISED);
  }
}

// =====================================================================
// TOUCH EVENT HANDLER
// =====================================================================
// Called for real sensor events AND for serial test overrides.
static void onTouchEvent(TouchEvent ev) {
  if (ev == TOUCH_NONE) return;

  // PRIORITY RULE (step 8): while a water reminder is active, a 300 ms
  // hold means "I drank the water" and wins over every other touch.
  if (ev == TOUCH_ACK && waterNagging) {
    glassesOfWater++;
    lastWaterAt = millis();
    waterNagging = false;
    facesReact(EXP_HAPPY);
    Serial.print(F("[water] drank! glasses="));
    Serial.println(glassesOfWater);
    // Sound and servo come in steps 4-5.
    return;
  }

  switch (ev) {
    case TOUCH_TAP:
      // Single tap -> LOVED face. Purr sound and servo wiggle come later.
      facesReact(EXP_LOVED);
      Serial.println(F("[touch] single tap -> LOVED (purr + wiggle: steps 4-5)"));
      break;

    case TOUCH_DOUBLE:
      // Double tap -> show the clock on display B for 3 s.
      // The face on A does NOT change.
      infoPeekClock();
      Serial.println(F("[touch] double tap -> clock on B for 3s"));
      break;

    case TOUCH_ANNOY:
      // 5 fast taps -> annoyed! Head shake and low beep later.
      facesReact(EXP_CONFUSED);
      Serial.println(F("[touch] 5 taps -> annoyed! (beep + shake: steps 4-5)"));
      break;

    case TOUCH_HOLD:
      // 2 s hold -> eyes close, head leans toward the hand.
      holdEyesActive = true;
      facesHoldEyesClosed(true);
      Serial.println(F("[touch] hold -> eyes shut (lean: step 5)"));
      break;

    case TOUCH_RELEASE:
      // End of a hold -> open eyes, release lean.
      holdEyesActive = false;
      facesHoldEyesClosed(false);
      Serial.println(F("[touch] released -> eyes open"));
      break;

    case TOUCH_ACK:
      // ACK without an active water reminder: ignored (not a tap, not a hold yet).
      // Just log it so the developer can see the event.
      Serial.println(F("[touch] ack (300ms) - no water reminder active, ignored"));
      break;

    default:
      break;
  }
}

// =====================================================================
// SERIAL TEST COMMANDS  (real touch is active; serial commands remain as
// overrides for testing without the board. Cards, Wi-Fi etc still faked.)
// =====================================================================
static void printHelp() {
  Serial.println(F("\n--- Desky test commands ---"));
  Serial.println(F("faces : calm happy loved surprised confused sad"));
  Serial.println(F("        sleepy sleeping focus thirsty celebrate eating"));
  Serial.println(F("cards : work break sleep milk unknown idle  (work again ends session)"));
  Serial.println(F("touch : tap hold annoy drank clock   (serial overrides for the real sensor)"));
  Serial.println(F("panel : time 14:05 | notime | weather 28 | noweather"));
  Serial.println(F("gaze  : left right centre free"));
  Serial.println(F("other : blink night day dim bright tour stop scan help"));
  Serial.println(F("----------------------------------------\n"));
}

static bool expressionByName(const String &w, Expression &out) {
  for (uint8_t i = 0; i < EXPRESSION_COUNT; i++) {
    if (w.equalsIgnoreCase(facesName((Expression)i))) { out = (Expression)i; return true; }
  }
  return false;
}

static void handleCommand(String w) {
  w.trim();
  if (w.length() == 0) return;

  Expression e;
  if (expressionByName(w, e)) {                 // "happy", "sad", ...
    tourRunning = false;
    facesReact(e);
    Serial.print(F("[face] ")); Serial.println(facesName(e));
    return;
  }

  // --- fake clock / weather for display B ---
  if (w.startsWith("time ")) {
    int c = w.indexOf(':');
    int h = w.substring(5).toInt();
    int m = (c > 0) ? w.substring(c + 1).toInt() : 0;
    if (h < 0 || h > 23 || m < 0 || m > 59) { Serial.println(F("[?] use: time 14:05")); return; }
    fakeBaseMin = h * 60 + m;  fakeBaseMs = millis();  fakeTimeOn = true;
    Serial.print(F("[time] fake clock set to ")); Serial.println(w.substring(5));
    return;
  }
  if (w.startsWith("weather ")) {
    fakeTempC = w.substring(8).toInt();  fakeWeatherOn = true;
    Serial.print(F("[weather] fake temperature ")); Serial.println(fakeTempC);
    return;
  }
  if (w == "notime")    { fakeTimeOn    = false; Serial.println(F("[time] clock off"));    return; }
  if (w == "noweather") { fakeWeatherOn = false; Serial.println(F("[weather] off"));      return; }

  // --- faked card taps ---
  if      (w == "work")     onWorkCard();
  else if (w == "break")    onBreakCard();
  else if (w == "sleep")    onSleepCard();
  else if (w == "milk")     onMilkCard();
  else if (w == "unknown")  onUnknownCard();
  else if (w == "idle")     setMode(MODE_IDLE);

  // --- serial touch overrides (route through the shared handler) ---
  else if (w == "tap")      onTouchEvent(TOUCH_TAP);
  else if (w == "hold")     { onTouchEvent(holdEyesActive ? TOUCH_RELEASE : TOUCH_HOLD); }
  else if (w == "annoy")    onTouchEvent(TOUCH_ANNOY);
  else if (w == "drank")    { waterNagging = true; onTouchEvent(TOUCH_ACK); }
  else if (w == "clock")    onTouchEvent(TOUCH_DOUBLE);

  // --- gaze, which the servo will drive for real in step 5 ---
  else if (w == "left")     facesSetGaze(-1);
  else if (w == "right")    facesSetGaze(1);
  else if (w == "centre" || w == "center") facesSetGaze(0);
  else if (w == "free")     facesReleaseGaze();

  // --- animation knobs ---
  else if (w == "blink")    facesForceBlink();
  else if (w == "night")    { fakeNight = true;  facesSetSlow(true);
                              facesSetBase(faceForMode(mode)); Serial.println(F("[time] night")); }
  else if (w == "day")      { fakeNight = false; facesSetSlow(mode == MODE_SLEEP);
                              facesSetBase(faceForMode(mode)); Serial.println(F("[time] day")); }
  else if (w == "dim")      facesSetDim(true);
  else if (w == "bright")   facesSetDim(false);
  else if (w == "tour")     { tourRunning = true; tourIndex = 0; tourNextAt = 0;
                              Serial.println(F("[tour] showing every expression, 2 s each")); }
  else if (w == "stop")     { tourRunning = false; facesClearReaction();
                              holdEyesActive = false; facesHoldEyesClosed(false); }
  else if (w == "scan")     scanI2C();
  else if (w == "help" || w == "?") printHelp();
  else { Serial.print(F("[?] unknown command: ")); Serial.println(w); }
}

static void handleSerial() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') { handleCommand(buf); buf = ""; }
    else if (buf.length() < 24) buf += c;
  }
}

// Walks through every expression so all twelve can be eyeballed in one go.
static void updateTour(uint32_t now) {
  if (!tourRunning || (int32_t)(now - tourNextAt) < 0) return;
  Expression e = (Expression)(tourIndex % EXPRESSION_COUNT);
  facesSetBase(e);
  facesClearReaction();
  Serial.print(F("[tour] ")); Serial.println(facesName(e));
  tourIndex++;
  tourNextAt = now + TOUR_STEP_MS;
}

// =====================================================================
// DEBUG LINE: mode, FPS on display A, free heap
// =====================================================================
static void updateDebug(uint32_t now) {
  if (now - lastDebugAt < DEBUG_PRINT_MS) return;
  fps         = frameCount;           // a second has passed, so frames == FPS
  frameCount  = 0;
  lastDebugAt = now;

  Serial.print(F("[dbg] mode="));  Serial.print(MODE_NAMES[mode]);
  Serial.print(F(" face="));       Serial.print(facesName(facesCurrent()));
  Serial.print(F(" fpsA="));       Serial.print(fps);
  Serial.print(F(" heap="));       Serial.print(ESP.getFreeHeap());
  Serial.print(F(" water="));      Serial.print(glassesOfWater);
  Serial.print(F(" dispB="));      Serial.print(infoDisplayFound ? F("yes") : F("no"));
  Serial.print(F(" drawsB="));     Serial.print(infoDrawCount());   // total real redraws of B
  if (mode == MODE_WORK) {
    Serial.print(F(" worked="));
    Serial.print((millis() - workStartedAt) / 60000UL);
    Serial.print(F("min"));
  }
  Serial.println();
}

// =====================================================================
// SETUP / LOOP
// =====================================================================
void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.println(F("\n\nDesky booting - step 3, face + info panel + touch"));

  // IMPORTANT: GPIO0 (buzzer) and GPIO2 (RC522 chip select) are boot-strap
  // pins and are deliberately left untouched here. They are configured in
  // steps 4 and 6, well after the board is past its boot sequence.

  // Bring the I2C bus up ourselves so the scan can run before any display
  // is initialised. Fast mode: two screens on 100 kHz would be far too slow.
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(OLED_I2C_CLOCK_HZ);
  scanI2C();

  facesBegin();                       // display A
  infoBegin(infoDisplayFound);        // display B (skipped cleanly if absent)
  touchBegin();                       // TTP223 on PIN_TOUCH
  setMode(MODE_IDLE);
  printHelp();
}

void loop() {
  uint32_t now = millis();

  handleSerial();
  onTouchEvent(touchUpdate(now));    // real TTP223 sensor
  updateTour(now);
  updateModeTimers(now);
  facesUpdate(now);               // blink / glance / breathe timers

  // Display B: only build and compare a new screen when it is allowed to
  // change (at most once a second). infoUpdate() draws nothing if the
  // content is the same as last time.
  if (infoDue(now)) {
    InfoInput in;
    fillInfoInput(now, in);
    infoUpdate(now, in);
  }

  // Display A redraws on a fixed interval. Display B, once it exists, will
  // redraw at most once a second and only when its content has changed.
  if (now - lastFrameAt >= FRAME_INTERVAL_MS) {
    lastFrameAt = now;
    facesRender();
    frameCount++;
    infoFlushStep();                // a slice of display B's new picture, if one is pending
  }

  updateDebug(now);

  // Not a delay: this hands a few microseconds to the Wi-Fi stack so it is
  // never starved. yield() / delay(0) are the only legal waits in here.
  yield();
}
