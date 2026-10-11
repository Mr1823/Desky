/*
 * faces.cpp - all of Desky's face drawing and the animation engine.
 *
 * This file drives DISPLAY A (0x3C) only, and draws nothing but the face.
 *
 * Reading guide:
 *   1. Geometry constants              - where the eyes and mouth sit
 *   2. Small drawing helpers           - eye shapes, mouth shapes, icons
 *   3. The style table                 - one row per expression
 *   4. Animation state + facesUpdate() - blinking, glancing, breathing
 *   5. facesRender()                   - puts one frame together
 *
 * There is no delay() and no while-loop that waits for anything.
 */
#include "faces.h"
#include "config.h"

// Full-buffer ("F") mode: the whole 1 KB frame lives in RAM, we draw into
// it and push it out in one I2C burst. Page mode would be smaller but it
// tears badly and cannot hit 20 FPS.
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
    U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);

// =====================================================================
// 1. GEOMETRY  - change these to redesign the face
// =====================================================================
#define FACE_CX      64     // horizontal centre of the screen
#define EYE_CY       27     // eye centre line
#define EYE_W        30     // normal eye width
#define EYE_H        34     // normal eye height
#define EYE_R        9      // corner rounding -> cute, not robotic
#define EYE_GAP      28     // empty space between the two eyes
#define MOUTH_CY     52     // mouth centre line

// Derived: the x centre of each eye.
#define EYE_L_CX     (FACE_CX - (EYE_GAP / 2) - (EYE_W / 2))   // 35
#define EYE_R_CX     (FACE_CX + (EYE_GAP / 2) + (EYE_W / 2))   // 93

// =====================================================================
// 2. SMALL DRAWING HELPERS
// =====================================================================

// A smooth 0 -> +amp -> 0 -> -amp -> 0 wave, driven purely by millis().
// Used for breathing, bouncing and all the little floating icons.
static const int8_t SINE16[16] = {
    0, 49, 90, 118, 127, 118, 90, 49, 0, -49, -90, -118, -127, -118, -90, -49};

static int16_t wave(uint32_t now, uint32_t periodMs, int16_t amp) {
  if (periodMs == 0) return 0;
  uint8_t i = (uint8_t)(((now % periodMs) * 16UL) / periodMs);
  return (int16_t)((int32_t)SINE16[i] * amp / 127);
}

// A filled rounded rectangle, with the corner radius kept legal for the
// size. U8g2 draws garbage if the radius is too big for the box.
static void drawRoundBox(int16_t cx, int16_t cy, int16_t w, int16_t h, int16_t r) {
  if (w < 1 || h < 1) return;
  int16_t maxR = (min(w, h) / 2) - 1;
  if (r > maxR) r = maxR;
  if (r < 1) u8g2.drawBox(cx - w / 2, cy - h / 2, w, h);
  else       u8g2.drawRBox(cx - w / 2, cy - h / 2, w, h, r);
}

// An upward curve "⌒" used for happy / closed eyes. 'rise' is how high
// the middle sits above the ends. 'thick' is the line weight.
static void drawArc(int16_t cx, int16_t cy, int16_t w, int16_t rise, uint8_t thick) {
  int16_t hw = w / 2;
  if (hw < 1) return;
  for (int16_t x = -hw; x <= hw; x++) {
    int16_t y = cy - rise + (int16_t)(((int32_t)x * x * rise) / ((int32_t)hw * hw));
    for (uint8_t t = 0; t < thick; t++) u8g2.drawPixel(cx + x, y + t);
  }
}

// A heart, for the LOVED face. Two discs plus a triangle.
static void drawHeart(int16_t cx, int16_t cy, int16_t s) {
  if (s < 3) return;
  u8g2.drawDisc(cx - s / 2, cy - s / 2, s / 2);
  u8g2.drawDisc(cx + s / 2, cy - s / 2, s / 2);
  u8g2.drawTriangle(cx - s, cy - s / 2, cx + s, cy - s / 2, cx, cy + s);
}

// The mouth. curve > 0 is a smile (middle dips down, corners lift),
// curve < 0 is a frown, curve == 0 is a flat determined line.
static void drawMouthCurve(int16_t cx, int16_t cy, int16_t w, int16_t curve) {
  int16_t hw = w / 2;
  if (hw < 1) return;
  for (int16_t x = -hw; x <= hw; x++) {
    int32_t k = (int32_t)hw * hw - (int32_t)x * x;     // 0 at the ends
    int16_t y = cy + (int16_t)((k * curve) / ((int32_t)hw * hw));
    u8g2.drawPixel(cx + x, y);
    u8g2.drawPixel(cx + x, y + 1);                     // 2 px thick
  }
}

// A small round "o" mouth: surprised, thirsty, chewing.
static void drawMouthO(int16_t cx, int16_t cy, int16_t r) {
  if (r < 2) r = 2;
  u8g2.drawCircle(cx, cy, r);
  u8g2.drawCircle(cx, cy, r - 1);
}

// A wide open laughing mouth: a filled half-disc with a flat top.
static void drawMouthLaugh(int16_t cx, int16_t cy, int16_t r) {
  u8g2.drawDisc(cx, cy, r);
  u8g2.setDrawColor(0);
  u8g2.drawBox(cx - r - 1, cy - r - 1, 2 * r + 3, r + 1);   // erase the top half
  u8g2.setDrawColor(1);
  u8g2.drawHLine(cx - r, cy, 2 * r + 1);
}

// A zig-zag mouth for CONFUSED. Drawn as joined line segments - plotting
// the points on their own leaves a row of disconnected dashes.
static void drawMouthWavy(int16_t cx, int16_t cy, int16_t w) {
  const int16_t step = 5;          // horizontal length of one zig
  const int16_t amp  = 2;          // how far up and down it swings
  int16_t hw = (w / (2 * step)) * step;
  for (int16_t x = -hw; x < hw; x += step) {
    int16_t y0 = cy + (((x + hw) / step) % 2 ? amp : -amp);
    int16_t y1 = cy + (((x + hw) / step) % 2 ? -amp : amp);
    u8g2.drawLine(cx + x, y0,     cx + x + step, y1);
    u8g2.drawLine(cx + x, y0 + 1, cx + x + step, y1 + 1);   // 2 px thick
  }
}

// A water droplet icon for THIRSTY.
static void drawDroplet(int16_t x, int16_t y) {
  u8g2.drawDisc(x, y + 4, 4);
  u8g2.drawTriangle(x - 4, y + 4, x + 4, y + 4, x, y - 5);
}

// A glass of milk for EATING. 'tilt' shifts the top sideways.
static void drawMilkGlass(int16_t x, int16_t y, int16_t tilt) {
  u8g2.drawLine(x - 7 + tilt, y - 10, x - 5, y + 10);      // left wall
  u8g2.drawLine(x + 7 + tilt, y - 10, x + 5, y + 10);      // right wall
  u8g2.drawLine(x - 5, y + 10, x + 5, y + 10);             // base
  u8g2.drawLine(x - 7 + tilt, y - 10, x + 7 + tilt, y - 10); // rim
  for (int16_t i = 0; i < 8; i++)                          // the milk
    u8g2.drawHLine(x - 5 + (tilt * (8 - i)) / 12, y + 2 + i - 1, 10);
}

// A 4-point sparkle for CELEBRATE.
static void drawSparkle(int16_t x, int16_t y, int16_t s) {
  if (s < 1) return;
  u8g2.drawVLine(x, y - s, 2 * s + 1);
  u8g2.drawHLine(x - s, y, 2 * s + 1);
  if (s > 2) {
    u8g2.drawPixel(x - 1, y - 1); u8g2.drawPixel(x + 1, y - 1);
    u8g2.drawPixel(x - 1, y + 1); u8g2.drawPixel(x + 1, y + 1);
  }
}

// =====================================================================
// 3. THE STYLE TABLE - one row per expression
// =====================================================================
enum EyeShape   { EYES_BOX, EYES_ARC, EYES_HEART, EYES_CLOSED };
enum MouthShape { MOUTH_CURVE, MOUTH_ROUND, MOUTH_LAUGH, MOUTH_WAVY };

struct FaceStyle {
  uint8_t eyeShape;
  uint8_t eyeW;
  uint8_t eyeH;
  int8_t  eyeDY;        // nudge the eyes up (-) or down (+)
  uint8_t mouthShape;
  uint8_t mouthSize;    // width for curves, radius for round mouths
  int8_t  mouthCurve;   // + smile, 0 flat, - frown
};

// The order of these rows MUST match the Expression enum in faces.h.
static const FaceStyle STYLES[EXPRESSION_COUNT] = {
  /* CALM      */ { EYES_BOX,    30, 34,  0, MOUTH_CURVE, 24,  3 },
  /* HAPPY     */ { EYES_ARC,    30,  8,  2, MOUTH_CURVE, 34,  7 },
  /* LOVED     */ { EYES_HEART,  20, 20,  0, MOUTH_CURVE, 28,  6 },
  /* SURPRISED */ { EYES_BOX,    34, 36,  0, MOUTH_ROUND,  6,  0 },
  /* CONFUSED  */ { EYES_BOX,    30, 34,  0, MOUTH_WAVY,  20,  0 },
  /* SAD       */ { EYES_BOX,    30, 28,  3, MOUTH_CURVE, 24, -5 },
  /* SLEEPY    */ { EYES_BOX,    30, 14,  9, MOUTH_CURVE, 16,  2 },
  /* SLEEPING  */ { EYES_CLOSED, 30,  4,  2, MOUTH_CURVE, 12,  2 },
  /* FOCUS     */ { EYES_BOX,    30, 16,  0, MOUTH_CURVE, 22,  0 },
  /* THIRSTY   */ { EYES_BOX,    30, 32,  0, MOUTH_ROUND,  5,  0 },
  /* CELEBRATE */ { EYES_ARC,    30,  8,  0, MOUTH_LAUGH,  9,  0 },
  /* EATING    */ { EYES_ARC,    28,  8,  2, MOUTH_ROUND,  5,  0 },
};

static const char *const NAMES[EXPRESSION_COUNT] = {
  "CALM", "HAPPY", "LOVED", "SURPRISED", "CONFUSED", "SAD",
  "SLEEPY", "SLEEPING", "FOCUS", "THIRSTY", "CELEBRATE", "EATING"
};

// =====================================================================
// 4. ANIMATION STATE
// =====================================================================
static Expression baseExpr    = EXP_CALM;
static Expression reactExpr   = EXP_CALM;
static bool       reacting    = false;
static uint32_t   reactUntil  = 0;

// Blinking is a tiny 4-step state machine.
enum BlinkPhase { BLINK_NONE, BLINK_CLOSING, BLINK_SHUT, BLINK_OPENING };
static uint8_t  blinkPhase    = BLINK_NONE;
static uint32_t blinkStarted  = 0;
static uint32_t nextBlinkAt   = 0;
static bool     secondBlink   = false;   // true while a double blink is queued

// Glancing left / right
static int8_t   gazeTarget    = 0;       // -1, 0, +1
static int16_t  gazePx        = 0;       // current pixel offset (eased)
static uint32_t nextLookAt    = 0;
static uint32_t lookEndsAt    = 0;
static bool     gazeExternal  = false;   // true = the servo is steering it
static uint32_t lastEaseAt    = 0;

static bool     lively        = false;
static bool     slow          = false;
static uint32_t nowMs         = 0;

// Every random interval is multiplied by this, so night mode is calmer.
static uint32_t scaled(uint32_t ms) { return slow ? ms * NIGHT_SLOWDOWN : ms; }

static void scheduleBlink() {
  uint32_t lo = lively ? LIVELY_BLINK_MIN_MS : BLINK_MIN_MS;
  uint32_t hi = lively ? LIVELY_BLINK_MAX_MS : BLINK_MAX_MS;
  nextBlinkAt = nowMs + scaled(random(lo, hi));    // never a fixed interval
}

static void scheduleLook() {
  uint32_t lo = lively ? LIVELY_LOOK_MIN_MS : LOOK_MIN_MS;
  uint32_t hi = lively ? LIVELY_LOOK_MAX_MS : LOOK_MAX_MS;
  nextLookAt = nowMs + scaled(random(lo, hi));
}

void facesBegin() {
  // Address and bus speed must be set BEFORE begin(), because begin() is
  // what actually talks to the panel. U8g2 wants the address pre-shifted.
  u8g2.setI2CAddress(OLED_FACE_ADDR << 1);
  u8g2.setBusClock(OLED_I2C_CLOCK_HZ);
  u8g2.begin();
  u8g2.setContrast(OLED_CONTRAST_FULL);
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  randomSeed(micros());
  nowMs = millis();
  scheduleBlink();
  scheduleLook();
}

// ---- choosing the expression ----------------------------------------
void facesSetBase(Expression e) { baseExpr = e; }

void facesReactFor(Expression e, uint32_t ms) {
  reactExpr  = e;
  reacting   = true;
  reactUntil = millis() + ms;
}

void facesReact(Expression e)  { facesReactFor(e, REACTION_HOLD_MS); }
void facesClearReaction()      { reacting = false; }
Expression facesCurrent()      { return reacting ? reactExpr : baseExpr; }
Expression facesBase()         { return baseExpr; }
bool facesIsReacting()         { return reacting; }
const char *facesName(Expression e) {
  return (e < EXPRESSION_COUNT) ? NAMES[e] : "?";
}
U8G2 &facesDisplay() { return u8g2; }

// ---- knobs ----------------------------------------------------------
void facesSetGaze(int8_t direction) {
  gazeTarget   = constrain(direction, -1, 1);
  gazeExternal = true;                // stop idle glancing, the head is boss
}

void facesReleaseGaze() {
  gazeExternal = false;
  gazeTarget   = 0;
  scheduleLook();
}

void facesSetLively(bool on) {
  if (on == lively) return;
  lively = on;
  scheduleBlink();
  scheduleLook();
}

void facesSetSlow(bool on) { slow = on; }
void facesSetDim(bool on)  { u8g2.setContrast(on ? OLED_CONTRAST_DIM : OLED_CONTRAST_FULL); }

void facesForceBlink() {
  blinkPhase   = BLINK_CLOSING;
  blinkStarted = millis();
  secondBlink  = false;
}

// =====================================================================
//    facesUpdate() - all the timers. Called every loop, never blocks.
// =====================================================================
void facesUpdate(uint32_t now) {
  nowMs = now;

  // (a) a reaction that has run out of time falls back to the base face
  if (reacting && (int32_t)(now - reactUntil) >= 0) reacting = false;

  // (b) blink state machine
  switch (blinkPhase) {
    case BLINK_NONE:
      if ((int32_t)(now - nextBlinkAt) >= 0) {
        blinkPhase   = BLINK_CLOSING;
        blinkStarted = now;
        // Sometimes a blink turns into a double blink. Small detail, but
        // it is the thing that stops the idle loop looking like a loop.
        secondBlink  = (random(100) < BLINK_DOUBLE_CHANCE);
      }
      break;
    case BLINK_CLOSING:
      if (now - blinkStarted >= BLINK_CLOSE_MS) { blinkPhase = BLINK_SHUT; blinkStarted = now; }
      break;
    case BLINK_SHUT:
      if (now - blinkStarted >= BLINK_HOLD_MS)  { blinkPhase = BLINK_OPENING; blinkStarted = now; }
      break;
    case BLINK_OPENING:
      if (now - blinkStarted >= BLINK_OPEN_MS) {
        blinkPhase = BLINK_NONE;
        if (secondBlink) { secondBlink = false; nextBlinkAt = now + BLINK_DOUBLE_GAP_MS; }
        else             { scheduleBlink(); }
      }
      break;
  }

  // (c) random glances, only while the servo is not steering the gaze
  if (!gazeExternal) {
    if (gazeTarget != 0 && (int32_t)(now - lookEndsAt) >= 0) {
      gazeTarget = 0;
      scheduleLook();
    } else if (gazeTarget == 0 && (int32_t)(now - nextLookAt) >= 0) {
      gazeTarget = (random(2) == 0) ? -1 : 1;
      lookEndsAt = now + scaled(LOOK_HOLD_MS);
    }
  }

  // (d) ease the eyes toward the target instead of snapping there
  if (now - lastEaseAt >= FRAME_INTERVAL_MS) {
    lastEaseAt = now;
    int16_t want = gazeTarget * GAZE_SHIFT_PX;
    if      (gazePx < want) gazePx += min((int16_t)GAZE_SPEED_PX, (int16_t)(want - gazePx));
    else if (gazePx > want) gazePx -= min((int16_t)GAZE_SPEED_PX, (int16_t)(gazePx - want));
  }
}

// =====================================================================
// 5. DRAWING ONE FRAME
// =====================================================================

// Eyelids that slant down and away from the nose. This is the single
// detail that makes SAD read as sad instead of as sleepy.
static void eraseSadLids(int16_t lx, int16_t rx, int16_t cy, int16_t w, int16_t h) {
  int16_t top = cy - h / 2;
  u8g2.setDrawColor(0);
  u8g2.drawTriangle(lx - w / 2, top - 1, lx + w / 2, top - 1, lx - w / 2, top + 12);
  u8g2.drawTriangle(rx - w / 2, top - 1, rx + w / 2, top - 1, rx + w / 2, top + 12);
  u8g2.setDrawColor(1);
}

// Angry-determined eyebrows for FOCUS: they slope inward and down.
static void drawFocusBrows(int16_t lx, int16_t rx, int16_t cy) {
  int16_t y = cy - 19;             // clear of the eye tops, or it looks muddy
  for (uint8_t t = 0; t < 2; t++) {
    u8g2.drawLine(lx - 13, y + t, lx + 13, y + 5 + t);
    u8g2.drawLine(rx + 13, y + t, rx - 13, y + 5 + t);
  }
}

// The floating "Z z z" of a sleeping pet. Each letter is on the same
// rising path, just a third of a cycle apart, so they chase each other.
static void drawSleepingZs(uint32_t now) {
  const uint16_t period = 2400;
  for (uint8_t i = 0; i < 3; i++) {
    uint32_t t = (now + (uint32_t)i * (period / 3)) % period;
    // Kept above y=22 so the letters never cross the closed eyes below.
    int16_t  x = 100 + (int16_t)(t * 18 / period);
    int16_t  y = 22  - (int16_t)(t * 16 / period);
    u8g2.setFont(i == 0 ? u8g2_font_10x20_tf : u8g2_font_6x10_tf);
    u8g2.drawStr(x, y, "Z");
  }
}

static void drawFace(uint32_t now) {
  Expression       e = facesCurrent();
  const FaceStyle &s = STYLES[e];

  // ---- the "alive" offsets -----------------------------------------
  // breathing: the whole face drifts 1 px up and down, very slowly.
  int16_t breath = wave(now, scaled(BREATH_PERIOD_MS), BREATH_AMPLITUDE_PX);
  int16_t bounce = lively ? wave(now, LIVELY_BOUNCE_MS, LIVELY_BOUNCE_PX) : 0;
  int16_t drift  = breath + bounce;

  int16_t eyeCy   = EYE_CY + drift + s.eyeDY;
  int16_t mouthCy = MOUTH_CY + drift;
  int16_t lx      = EYE_L_CX + gazePx;      // eyes follow the gaze / the head
  int16_t rx      = EYE_R_CX + gazePx;

  // ---- how open are the eyes right now? ----------------------------
  int16_t openPct = 100;
  if      (blinkPhase == BLINK_CLOSING) openPct = 100 - (int16_t)((now - blinkStarted) * 100 / BLINK_CLOSE_MS);
  else if (blinkPhase == BLINK_SHUT)    openPct = 0;
  else if (blinkPhase == BLINK_OPENING) openPct = (int16_t)((now - blinkStarted) * 100 / BLINK_OPEN_MS);
  openPct = constrain(openPct, (int16_t)0, (int16_t)100);

  // CONFUSED keeps one eye half shut. That asymmetry is the whole joke.
  int16_t rightPct = (e == EXP_CONFUSED) ? 55 : 100;

  // ---- eyes ---------------------------------------------------------
  switch (s.eyeShape) {
    case EYES_BOX: {
      int16_t hl = max((int16_t)2, (int16_t)(s.eyeH * openPct / 100));
      int16_t hr = max((int16_t)2, (int16_t)(s.eyeH * openPct / 100 * rightPct / 100));
      drawRoundBox(lx, eyeCy, s.eyeW, hl, EYE_R);
      drawRoundBox(rx, eyeCy, s.eyeW, hr, EYE_R);
      if (e == EXP_SAD) eraseSadLids(lx, rx, eyeCy, s.eyeW, hl);
      if (e == EXP_FOCUS) drawFocusBrows(lx, rx, eyeCy);
      break;
    }
    case EYES_ARC:                      // happy / celebrating / eating
      drawArc(lx, eyeCy + s.eyeH / 2, s.eyeW, s.eyeH, 4);
      drawArc(rx, eyeCy + s.eyeH / 2, s.eyeW, s.eyeH, 4);
      break;

    case EYES_HEART: {                  // LOVED
      int16_t sz = max((int16_t)3, (int16_t)(9 * openPct / 100));
      drawHeart(lx, eyeCy, sz);
      drawHeart(rx, eyeCy, sz);
      break;
    }
    case EYES_CLOSED:                   // SLEEPING
      drawArc(lx, eyeCy, s.eyeW, s.eyeH, 3);
      drawArc(rx, eyeCy, s.eyeW, s.eyeH, 3);
      break;
  }

  // ---- mouth --------------------------------------------------------
  switch (s.mouthShape) {
    case MOUTH_CURVE:
      drawMouthCurve(FACE_CX + gazePx / 2, mouthCy, s.mouthSize, s.mouthCurve);
      break;
    case MOUTH_ROUND: {
      int16_t r = s.mouthSize;
      // EATING chews: the mouth opens and shuts about twice a second.
      if (e == EXP_EATING) r = 4 + abs(wave(now, 500, 3));
      drawMouthO(FACE_CX + gazePx / 2, mouthCy, r);
      break;
    }
    case MOUTH_LAUGH:
      drawMouthLaugh(FACE_CX + gazePx / 2, mouthCy - 2, s.mouthSize);
      break;
    case MOUTH_WAVY:
      drawMouthWavy(FACE_CX + gazePx / 2, mouthCy, s.mouthSize);
      break;
  }

  // ---- per-expression props ----------------------------------------
  switch (e) {
    case EXP_CONFUSED:
      u8g2.setFont(u8g2_font_10x20_tf);
      u8g2.drawStr(112, 20 + wave(now, 1200, 2), "?");
      break;

    case EXP_SLEEPY:
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.drawStr(108, 20 - (int16_t)((now % 2000) * 8 / 2000), "z");
      break;

    case EXP_SLEEPING:
      drawSleepingZs(now);
      break;

    case EXP_THIRSTY:
      drawDroplet(119, 24 + wave(now, 900, 2));
      break;

    case EXP_CELEBRATE: {
      static const int8_t SPARK[4][2] = {{12, 12}, {116, 14}, {16, 58}, {112, 56}};
      for (uint8_t i = 0; i < 4; i++)
        drawSparkle(SPARK[i][0], SPARK[i][1], 2 + abs(wave(now + i * 250, 1000, 2)));
      break;
    }
    case EXP_EATING:
      drawMilkGlass(112, 46, wave(now, 1300, 3));
      break;

    default:
      break;
  }
}

void facesRender() {
  u8g2.clearBuffer();
  drawFace(nowMs);
  u8g2.sendBuffer();     // one I2C burst, ~25 ms at 400 kHz
}
