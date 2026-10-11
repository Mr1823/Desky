/*
 * infopanel.cpp - display B. See infopanel.h for the big picture.
 *
 * Reading guide:
 *   1. The panel + fonts
 *   2. Snap: "what is on the screen", and how two Snaps are compared
 *   3. Transient screens (summary / mode name / clock peek)
 *   4. Drawing each view
 *   5. infoDue / infoUpdate / infoFlushStep
 */
#include "infopanel.h"
#include "config.h"
#include <U8g2lib.h>
#include <string.h>

// =====================================================================
// 1. THE PANEL + FONTS
// =====================================================================
// A second U8g2 object, on the SAME two I2C wires as display A. The address
// tells them apart (set in infoBegin).
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C panel(
    U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);

#define FONT_HUGE   u8g2_font_logisoso32_tr
#define FONT_BIG    u8g2_font_logisoso24_tr
#define FONT_MID    u8g2_font_logisoso16_tr
#define FONT_BOLD   u8g2_font_7x13B_tr
#define FONT_SMALL  u8g2_font_6x12_tr

static bool     present    = false;
static bool     drawnOnce  = false;
static uint32_t lastDrawAt = 0;
static uint16_t drawCount  = 0;
static bool     panelDim   = false;

// Sliced transfer state: which page goes out next.
static bool     flushing   = false;
static uint8_t  nextPage   = 0;

// =====================================================================
// 2. SNAP - the complete description of one screen
// =====================================================================
enum View : uint8_t {
  V_BLANK = 0,    // SLEEP with no clock yet
  V_IDLE,         // time, date, weather
  V_WORK,         // session timer, water count, next reminder
  V_BREAK,        // break countdown
  V_SLEEP_TIME,   // dim clock only
  V_FLASH,        // the mode name
  V_SUMMARY,      // end-of-session summary
  V_CLOCK         // double-tap clock peek
};

struct Snap {
  uint8_t  view;
  bool     dim;
  bool     timeValid;
  uint8_t  hour, minute, mday, month, wday;
  bool     weatherValid;
  int16_t  tempC;
  uint32_t seconds;      // WORK timer, BREAK countdown, or minutes in a summary
  uint16_t glasses;
  uint16_t mins;         // minutes until water
  char     name[8];      // mode name for V_FLASH
};

// Field by field on purpose: memcmp() on a struct can be fooled by padding
// bytes that hold random junk.
static bool sameSnap(const Snap &a, const Snap &b) {
  return a.view == b.view && a.dim == b.dim && a.timeValid == b.timeValid &&
         a.hour == b.hour && a.minute == b.minute && a.mday == b.mday &&
         a.month == b.month && a.wday == b.wday &&
         a.weatherValid == b.weatherValid && a.tempC == b.tempC &&
         a.seconds == b.seconds && a.glasses == b.glasses && a.mins == b.mins &&
         strcmp(a.name, b.name) == 0;
}

static Snap lastSnap;

// =====================================================================
// 3. TRANSIENT SCREENS
// =====================================================================
// A transient is "armed" when requested. Its timer only starts once it has
// really been drawn, so a screen is never cut short just because the
// once-per-second redraw limit delayed it.
struct Transient {
  bool     pending;
  bool     shown;
  uint32_t shownAt;
  uint32_t durMs;
};

static Transient trSummary, trFlash, trClock;
static uint32_t  sumMinutes = 0;
static uint16_t  sumGlasses = 0;
static char      flashName[8] = "";

static void arm(Transient &t, uint32_t durMs) {
  t.pending = true;
  t.shown   = false;
  t.durMs   = durMs;
}

// true while the transient should still be on screen
static bool alive(Transient &t, uint32_t now) {
  if (t.pending && t.shown && (now - t.shownAt) >= t.durMs) t.pending = false;
  return t.pending;
}

static void markShown(uint8_t view, uint32_t now) {
  Transient *t = (view == V_SUMMARY) ? &trSummary
               : (view == V_FLASH)   ? &trFlash
               : (view == V_CLOCK)   ? &trClock : nullptr;
  if (t && t->pending && !t->shown) { t->shown = true; t->shownAt = now; }
}

void infoFlashMode(const char *name) {
  Serial.print(F("[infoB] mode name \""));
  Serial.print(name);
  Serial.println(F("\" for 2s, then back to normal content"));
  strncpy(flashName, name, sizeof(flashName) - 1);
  flashName[sizeof(flashName) - 1] = 0;
  arm(trFlash, INFO_MODE_FLASH_MS);
}

void infoShowSummary(uint32_t minutes, uint16_t glasses) {
  Serial.print(F("[infoB] summary for 5s: "));
  Serial.print(minutes);  Serial.print(F(" min worked, "));
  Serial.print(glasses);  Serial.println(F(" glasses of water"));
  sumMinutes = minutes;
  sumGlasses = glasses;
  arm(trSummary, SUMMARY_SCREEN_MS);
  trFlash.pending = false;       // the summary replaces the mode-name flash
}

void infoPeekClock() {
  Serial.println(F("[infoB] clock for 3s (face on A unchanged)"));
  arm(trClock, CLOCK_SCREEN_MS);
}

// =====================================================================
// 4. DRAWING
// =====================================================================
static const char *const DAY_NAMES[7]    = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const MONTH_NAMES[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Draw a string centred horizontally. y is the baseline.
static void centred(const uint8_t *font, int16_t y, const char *s) {
  panel.setFont(font);
  panel.drawStr((128 - panel.getStrWidth(s)) / 2, y, s);
}

static void fmtTime(char *out, size_t n, const Snap &s) {
  if (s.timeValid) snprintf(out, n, "%02u:%02u", (unsigned)s.hour, (unsigned)s.minute);
  else             snprintf(out, n, "--:--");
}

static void fmtDate(char *out, size_t n, const Snap &s) {
  if (s.timeValid && s.wday < 7 && s.month >= 1 && s.month <= 12)
    snprintf(out, n, "%s, %u %s", DAY_NAMES[s.wday], (unsigned)s.mday, MONTH_NAMES[s.month - 1]);
  else
    snprintf(out, n, "waiting for time");
}

// Seconds -> "MM:SS", or "H:MM:SS" once it passes an hour.
static void fmtTimer(char *out, size_t n, uint32_t sec) {
  if (sec >= 3600UL)
    snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)(sec / 3600UL),
             (unsigned long)((sec / 60UL) % 60UL), (unsigned long)(sec % 60UL));
  else
    snprintf(out, n, "%02lu:%02lu", (unsigned long)(sec / 60UL), (unsigned long)(sec % 60UL));
}

// "28" + a small degree circle + "C". The circle is drawn as a shape because
// not every font has a degree sign.
static void drawWeather(int16_t y, const Snap &s) {
  if (!s.weatherValid) { centred(FONT_SMALL, y, "weather --"); return; }
  char num[8];
  snprintf(num, sizeof(num), "%d", (int)s.tempC);
  panel.setFont(FONT_SMALL);
  int16_t wNum = panel.getStrWidth(num);
  int16_t wC   = panel.getStrWidth("C");
  int16_t x    = (128 - (wNum + 8 + wC)) / 2;
  panel.drawStr(x, y, num);
  panel.drawCircle(x + wNum + 3, y - 8, 2);
  panel.drawStr(x + wNum + 7, y, "C");
}

static void drawSnap(const Snap &s) {
  char big[16], line[24];
  panel.clearBuffer();
  panel.setDrawColor(1);

  switch (s.view) {
    case V_IDLE:
      fmtTime(big, sizeof(big), s);
      centred(FONT_BIG, 28, big);
      fmtDate(line, sizeof(line), s);
      centred(FONT_SMALL, 45, line);
      drawWeather(62, s);
      break;

    case V_WORK: {
      centred(FONT_SMALL, 11, "WORK");
      fmtTimer(big, sizeof(big), s.seconds);
      centred(FONT_BIG, 42, big);
      panel.drawHLine(0, 47, 128);
      panel.setFont(FONT_SMALL);
      snprintf(line, sizeof(line), "Water: %u", (unsigned)s.glasses);
      panel.drawStr(0, 61, line);
      if (s.mins == 0) snprintf(line, sizeof(line), "Drink now");
      else             snprintf(line, sizeof(line), "Drink in %um", (unsigned)s.mins);
      panel.drawStr(128 - panel.getStrWidth(line), 61, line);
      break;
    }

    case V_BREAK:
      centred(FONT_SMALL, 12, "BREAK");
      fmtTimer(big, sizeof(big), s.seconds);
      centred(FONT_HUGE, 52, big);
      break;

    case V_SLEEP_TIME:
      fmtTime(big, sizeof(big), s);
      centred(FONT_MID, 38, big);
      break;

    case V_FLASH:
      centred(FONT_BIG, 42, s.name);
      break;

    case V_SUMMARY:
      centred(FONT_BOLD, 14, "SESSION DONE");
      panel.drawHLine(0, 20, 128);
      snprintf(line, sizeof(line), "%lu min worked", (unsigned long)s.seconds);
      centred(FONT_SMALL, 38, line);
      snprintf(line, sizeof(line), "%u %s of water", (unsigned)s.glasses,
               s.glasses == 1 ? "glass" : "glasses");
      centred(FONT_SMALL, 56, line);
      break;

    case V_CLOCK:
      fmtTime(big, sizeof(big), s);
      centred(FONT_HUGE, 40, big);
      fmtDate(line, sizeof(line), s);
      centred(FONT_SMALL, 58, line);
      break;

    case V_BLANK:
    default:
      break;                 // an empty, black panel
  }
}

// =====================================================================
// 5. THE PUBLIC LOOP FUNCTIONS
// =====================================================================
bool infoBegin(bool displayPresent) {
  present = displayPresent;
  if (!present) {
    Serial.println(F("[infoB] display B absent - everything for B is skipped"));
    return false;
  }
  // Same rule as display A: address and clock BEFORE begin(), and U8g2
  // wants the 7-bit address pre-shifted into its 8-bit form.
  panel.setI2CAddress(OLED_INFO_ADDR << 1);
  panel.setBusClock(OLED_I2C_CLOCK_HZ);
  panel.begin();
  panel.setContrast(OLED_CONTRAST_FULL);
  panel.clearBuffer();
  panel.sendBuffer();                 // one-off full clear at boot
  memset(&lastSnap, 0, sizeof(lastSnap));
  return true;
}

bool infoPresent() { return present; }

// Cheap test the main loop uses to skip building a Snap on most passes.
bool infoDue(uint32_t now) {
  if (!present) return false;
  if (flushing) return false;                         // previous picture still going out
  if (!drawnOnce) return true;
  return (now - lastDrawAt) >= INFO_MIN_REDRAW_MS;    // the once-per-second rule
}

void infoUpdate(uint32_t now, const InfoInput &in) {
  if (!infoDue(now)) return;

  // ---- 1. build a description of what SHOULD be on screen -------------
  Snap s;
  memset(&s, 0, sizeof(s));
  s.dim          = (in.mode == INFO_SLEEP);
  s.timeValid    = in.timeValid;
  s.hour         = in.hour;   s.minute = in.minute;
  s.mday         = in.mday;   s.month  = in.month;   s.wday = in.wday;
  s.weatherValid = in.weatherValid;
  s.tempC        = in.weatherValid ? in.tempC : 0;

  if (alive(trSummary, now)) {
    s.view = V_SUMMARY;  s.seconds = sumMinutes;  s.glasses = sumGlasses;
    s.timeValid = false; s.weatherValid = false; s.tempC = 0;   // not shown, so must not trigger a redraw
    s.hour = s.minute = s.mday = s.month = s.wday = 0;
  } else if (alive(trFlash, now)) {
    s.view = V_FLASH;
    strncpy(s.name, flashName, sizeof(s.name) - 1);
    s.weatherValid = false; s.tempC = 0;
    s.hour = s.minute = s.mday = s.month = s.wday = 0; s.timeValid = false;
  } else if (alive(trClock, now)) {
    s.view = V_CLOCK;
    s.weatherValid = false; s.tempC = 0;
  } else {
    switch (in.mode) {
      case INFO_WORK:
        s.view = V_WORK;  s.seconds = in.workSeconds;  s.glasses = in.glasses;  s.mins = in.minsToWater;
        s.timeValid = false; s.weatherValid = false; s.tempC = 0;
        s.hour = s.minute = s.mday = s.month = s.wday = 0;
        break;
      case INFO_BREAK:
        s.view = V_BREAK; s.seconds = in.breakSecondsLeft;
        s.timeValid = false; s.weatherValid = false; s.tempC = 0;
        s.hour = s.minute = s.mday = s.month = s.wday = 0;
        break;
      case INFO_SLEEP:
        s.view = in.timeValid ? V_SLEEP_TIME : V_BLANK;
        s.weatherValid = false; s.tempC = 0;
        s.mday = s.month = s.wday = 0;                 // only HH:MM is shown
        if (!in.timeValid) s.hour = s.minute = 0;
        break;
      case INFO_IDLE:
      default:
        s.view = V_IDLE;
        break;
    }
  }

  // ---- 2. unchanged? then draw nothing and send nothing ---------------
  if (drawnOnce && sameSnap(s, lastSnap)) return;

  // ---- 3. changed: draw into RAM, then let infoFlushStep() send it ----
  if (s.dim != panelDim) {
    panel.setContrast(s.dim ? OLED_CONTRAST_DIM : OLED_CONTRAST_FULL);
    panelDim = s.dim;
  }
  drawSnap(s);
  markShown(s.view, now);

  lastSnap   = s;
  drawnOnce  = true;
  lastDrawAt = now;
  drawCount++;
  flushing   = true;
  nextPage   = 0;
}

void infoFlushStep() {
  if (!present || !flushing) return;
  for (uint8_t i = 0; i < INFO_PAGES_PER_FRAME && flushing; i++) {
    panel.updateDisplayArea(0, nextPage, 16, 1);      // 16 tiles wide, 1 page tall
    if (++nextPage >= 8) flushing = false;
  }
}

uint16_t infoDrawCount() { return drawCount; }
