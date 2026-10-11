/*
 * Desky - a desk companion pet on a NodeMCU ESP-12E (ESP8266).
 *
 * ==== BUILD STEP 1 of 8 ====
 * I2C scan + display A (the face) with the full animation engine.
 *
 * Desky has two OLEDs on the same two I2C wires, told apart by address:
 *   display A, 0x3C - the FACE. Only ever the face. No text, no numbers.
 *   display B, 0x3D - the INFO PANEL. Every word and digit lives there.
 *
 * Display B is build step 2, so for now anything that would be printed on
 * it is sent to the serial monitor instead, tagged [infoB]. That keeps the
 * rule intact: nothing but the face ever reaches display A.
 *
 * The touch sensor, buzzer, servo, NFC reader and Wi-Fi are not wired up in
 * code yet either, so every input is faked over serial. Open the serial
 * monitor at 115200 baud and type "help".
 *
 * Golden rules (the long version is in config.h):
 *   - no delay() anywhere, every timer is a millis() deadline
 *   - nothing in loop() may block, the animation must never freeze
 */
#include <Wire.h>
#include "config.h"
#include "faces.h"

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

// Which displays actually answered on the bus. Desky must run fully even
// if display B is missing, so this is a flag and never an error.
static bool faceDisplayFound = false;
static bool infoDisplayFound = false;

// Night mode is faked with the "night" command until step 7 brings NTP in.
static bool fakeNight = false;

// FPS / debug
static uint32_t lastFrameAt = 0;
static uint32_t lastDebugAt = 0;
static uint16_t frameCount  = 0;
static uint16_t fps         = 0;

// The "tour" demo walks through every expression, 2 s each.
static bool     tourRunning = false;
static uint8_t  tourIndex   = 0;
static uint32_t tourNextAt  = 0;

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
// DISPLAY B STAND-IN
// =====================================================================
// Step 2 replaces every one of these calls with infopanel.cpp. Until then
// they print to serial, so the info-panel behaviour can already be checked.
static void infoShowMode(const char *name) {
  Serial.print(F("[infoB] mode name \""));
  Serial.print(name);
  Serial.print(F("\" for "));
  Serial.print(INFO_MODE_FLASH_MS / 1000);
  Serial.println(F("s, then back to normal content"));
}

static void infoShowSummary(uint32_t minutes, uint16_t glasses) {
  Serial.print(F("[infoB] summary for 5s: "));
  Serial.print(minutes);  Serial.print(F(" min worked, "));
  Serial.print(glasses);  Serial.println(F(" glasses of water"));
}

static void infoShowClock() {
  Serial.println(F("[infoB] clock for 3s (face on A unchanged)"));
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

static void setMode(Mode m) {
  if (m != MODE_EATING && m != MODE_BREAK) modeBefore = m;
  mode        = m;
  modeStarted = millis();

  facesSetBase(faceForMode(m));
  facesSetLively(m == MODE_BREAK);          // BREAK blinks and glances more
  facesSetDim(m == MODE_SLEEP);             // SLEEP dims the panel
  facesSetSlow(fakeNight || m == MODE_SLEEP);

  Serial.print(F("[mode] -> "));
  Serial.println(MODE_NAMES[m]);
  infoShowMode(MODE_NAMES[m]);
}

// Each card tap, faked over serial for now. The real reader arrives in
// step 6, but the behaviour here is already final.
static void onWorkCard() {
  if (mode == MODE_WORK) {
    // Tapping WORK again ends the session. The summary goes on display B;
    // the face celebrates on display A.
    uint32_t minutes = (millis() - workStartedAt) / 60000UL;
    infoShowSummary(minutes, glassesOfWater);
    glassesOfWater = 0;
    setMode(MODE_IDLE);
    facesReact(EXP_CELEBRATE);
  } else {
    workStartedAt  = millis();
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
  modeBefore  = (mode == MODE_EATING) ? modeBefore : mode;
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
    setMode(modeBefore);
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
// SERIAL FAKE INPUTS  (this whole section disappears as the real sensors
// are wired in over steps 3-8)
// =====================================================================
static void printHelp() {
  Serial.println(F("\n--- Desky step 1: face test commands ---"));
  Serial.println(F("faces : calm happy loved surprised confused sad"));
  Serial.println(F("        sleepy sleeping focus thirsty celebrate eating"));
  Serial.println(F("cards : work break sleep milk unknown idle  (work again ends session)"));
  Serial.println(F("touch : tap hold annoy drank clock"));
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

  // --- faked card taps ---
  if      (w == "work")     onWorkCard();
  else if (w == "break")    onBreakCard();
  else if (w == "sleep")    onSleepCard();
  else if (w == "milk")     onMilkCard();
  else if (w == "unknown")  onUnknownCard();
  else if (w == "idle")     setMode(MODE_IDLE);

  // --- faked touch reactions (real sensor in step 3) ---
  else if (w == "tap")      { facesReact(EXP_LOVED);  Serial.println(F("[touch] single tap -> LOVED")); }
  else if (w == "hold")     { facesReactFor(EXP_SLEEPING, 2000);
                              Serial.println(F("[touch] hold -> eyes close")); }
  else if (w == "annoy")    { facesReact(EXP_CONFUSED); Serial.println(F("[touch] 5 fast taps -> annoyed")); }
  else if (w == "drank")    { glassesOfWater++; facesReact(EXP_HAPPY);
                              Serial.print(F("[water] glasses=")); Serial.println(glassesOfWater); }
  else if (w == "clock")    infoShowClock();      // double tap: display B only

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
  else if (w == "stop")     { tourRunning = false; facesClearReaction(); }
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
  tourNextAt = now + 2000;
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
  Serial.println(F("\n\nDesky booting - step 1, I2C scan + face engine"));

  // IMPORTANT: GPIO0 (buzzer) and GPIO2 (RC522 chip select) are boot-strap
  // pins and are deliberately left untouched here. They are configured in
  // steps 4 and 6, well after the board is past its boot sequence.

  // Bring the I2C bus up ourselves so the scan can run before any display
  // is initialised. Fast mode: two screens on 100 kHz would be far too slow.
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(OLED_I2C_CLOCK_HZ);
  scanI2C();

  facesBegin();            // display A only
  setMode(MODE_IDLE);
  printHelp();
}

void loop() {
  uint32_t now = millis();

  handleSerial();
  updateTour(now);
  updateModeTimers(now);
  facesUpdate(now);               // blink / glance / breathe timers

  // Display A redraws on a fixed interval. Display B, once it exists, will
  // redraw at most once a second and only when its content has changed.
  if (now - lastFrameAt >= FRAME_INTERVAL_MS) {
    lastFrameAt = now;
    facesRender();
    frameCount++;
  }

  updateDebug(now);

  // Not a delay: this hands a few microseconds to the Wi-Fi stack so it is
  // never starved. yield() / delay(0) are the only legal waits in here.
  yield();
}
