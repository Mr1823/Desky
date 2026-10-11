/*
 * faces.h - Desky's face: drawing + the animation engine.
 *
 * This module owns DISPLAY A (address OLED_FACE_ADDR) and nothing else.
 * Display A shows the face and only the face - never text, numbers, a
 * clock or a menu, not even for a moment. All words and digits belong on
 * display B, which is infopanel.h/.cpp (build step 2).
 *
 * How to think about this module:
 *
 *   - There is a BASE expression. That is the face that belongs to the
 *     current mode (CALM when idle, FOCUS while working, SLEEPING at night).
 *     It stays until the mode changes.
 *
 *   - On top of that you can fire a REACTION: a short-lived expression
 *     (HAPPY, LOVED, CONFUSED...). After it expires the face falls back to
 *     the base expression on its own. Nobody has to remember to reset it.
 *
 *   - "Idle life" runs the whole time, on every expression: random blinks,
 *     occasional double blinks, random glances left and right, and a slow
 *     1-pixel breathing drift. This is what makes it read as alive instead
 *     of as a picture.
 *
 * Nothing in here blocks. facesUpdate() only looks at millis() and returns.
 */
#ifndef FACES_H
#define FACES_H

#include <Arduino.h>
#include <U8g2lib.h>

enum Expression {
  EXP_CALM = 0,
  EXP_HAPPY,
  EXP_LOVED,
  EXP_SURPRISED,
  EXP_CONFUSED,
  EXP_SAD,
  EXP_SLEEPY,
  EXP_SLEEPING,
  EXP_FOCUS,
  EXP_THIRSTY,
  EXP_CELEBRATE,
  EXP_EATING,
  EXPRESSION_COUNT
};

// ---- setup ----------------------------------------------------------
void facesBegin();

// ---- choosing what is on the face -----------------------------------
void       facesSetBase(Expression e);                 // the mode's face
void       facesReact(Expression e);                   // short reaction, auto-reverts
void       facesReactFor(Expression e, uint32_t ms);   // reaction with a custom length
void       facesClearReaction();                       // drop back to base now
Expression facesCurrent();
Expression facesBase();
bool       facesIsReacting();

// ---- extra behaviour knobs ------------------------------------------
// The head and the eyes must agree, so gestures.cpp calls facesSetGaze()
// whenever it turns the head. -1 = look left, 0 = centre, +1 = look right.
void facesSetGaze(int8_t direction);
void facesReleaseGaze();          // hand control back to the idle animation
void facesSetLively(bool on);     // BREAK mode: blink/glance more, bounce
void facesSetSlow(bool on);       // after 22:00: everything slower
void facesSetDim(bool on);        // SLEEP mode: dim the panel
void facesForceBlink();           // used by the "eyes closed" touch hold

// ---- per-loop work --------------------------------------------------
void facesUpdate(uint32_t now);   // advance timers. Call every loop.
void facesRender();               // draw one frame and push it to the OLED.

// ---- helpers --------------------------------------------------------
const char *facesName(Expression e);
U8G2       &facesDisplay();       // raw access for other modules

#endif // FACES_H
