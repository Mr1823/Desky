/*
 * infopanel.h - display B (address OLED_INFO_ADDR): every word and number.
 *
 * Display A shows only the face. Anything with letters or digits goes here.
 *
 * How it works, in three ideas:
 *
 *   1. desky.ino fills an InfoInput ("what is true right now") and hands it
 *      to infoUpdate(). This module decides WHAT to show: a short
 *      "transient" screen (summary, mode name, clock peek) wins over the
 *      normal screen for the current mode.
 *
 *   2. CHANGE DETECTION. The screen is described by a small Snap struct. We
 *      keep the Snap we last drew and compare before drawing. Same Snap =
 *      nothing is drawn and nothing is sent over I2C. Even when it did
 *      change, display B is redrawn at most once per second.
 *
 *   3. SLICED TRANSFER. A full redraw of a 128x64 OLED takes ~25 ms of I2C
 *      time, which would be one visibly slow frame on display A every
 *      second. So the picture is drawn into RAM at once (cheap) and sent
 *      to the panel a couple of pages per frame by infoFlushStep().
 *
 * If display B is not connected, every function here quietly does nothing.
 */
#ifndef INFOPANEL_H
#define INFOPANEL_H

#include <Arduino.h>

// The normal screen is chosen by mode. EATING is not listed: it "keeps the
// previous content", so desky.ino reports the mode that MILK interrupted.
enum InfoMode : uint8_t { INFO_IDLE = 0, INFO_WORK, INFO_BREAK, INFO_SLEEP };

struct InfoInput {
  uint8_t  mode;                 // an InfoMode value

  // clock (timeValid == false until Wi-Fi time arrives)
  bool     timeValid;
  uint8_t  hour, minute;
  uint8_t  mday, month;          // month 1-12
  uint8_t  wday;                 // 0 = Sunday

  // weather (weatherValid == false until a fetch has worked)
  bool     weatherValid;
  int16_t  tempC;

  // WORK
  uint32_t workSeconds;          // session timer
  uint16_t glasses;              // glasses of water so far
  uint16_t minsToWater;          // minutes until the next water reminder

  // BREAK
  uint32_t breakSecondsLeft;
};

bool infoBegin(bool displayPresent);   // call once in setup()
bool infoPresent();

bool infoDue(uint32_t now);            // true when it is worth calling infoUpdate()
void infoUpdate(uint32_t now, const InfoInput &in);   // decide + draw into RAM
void infoFlushStep();                  // send a slice of the new picture. Once per display-A frame.

// One-off screens. Each one is shown for its time (see config.h), counted
// from the moment it actually appears on the panel, then normal content returns.
void infoFlashMode(const char *name);                       // card tapped: mode name
void infoShowSummary(uint32_t minutes, uint16_t glasses);   // session ended
void infoPeekClock();                                       // double tap

uint16_t infoDrawCount();              // how many real redraws so far (debug)

#endif // INFOPANEL_H
