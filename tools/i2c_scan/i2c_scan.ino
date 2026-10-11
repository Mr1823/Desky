/*
 * i2c_scan.ino - a throwaway helper sketch. It does ONE job: print every
 * I2C address that answers on the bus.
 *
 * Use this FIRST, before building anything else. Desky needs two OLEDs on
 * the same two wires, told apart by address:
 *
 *   0x3C  display A - the face
 *   0x3D  display B - the info panel
 *
 * If you only see 0x3C, display B's address jumper has not been changed
 * yet. On most SSD1306 modules that is a solder bridge on the back labelled
 * something like "0x78 / 0x7A" (those are the 8-bit forms of 0x3C / 0x3D).
 *
 * Wiring, same as the main project:
 *   both OLEDs SCL -> D1 / GPIO5   (yellow)
 *   both OLEDs SDA -> D2 / GPIO4   (green)
 *   both OLEDs VCC -> 3.3V rail    (red)
 *   both OLEDs GND -> GND rail     (black)
 *
 * Read the labels on your module before wiring. Some are GND VCC SCL SDA
 * and some are VCC GND SCL SDA - swapping VCC and GND destroys the display.
 */
#include <Wire.h>

#define PIN_SCL  5      // D1
#define PIN_SDA  4      // D2

void setup() {
  Serial.begin(115200);
  delay(200);                 // fine in this throwaway tool, not in Desky
  Serial.println(F("\n\n--- Desky I2C scanner ---"));

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);      // the same fast mode the main sketch uses
}

void loop() {
  Serial.println(F("\nScanning 0x01 - 0x7E ..."));
  uint8_t found = 0;

  for (uint8_t addr = 1; addr < 127; addr++) {
    // A zero-length write is the standard way to ask "is anyone there?".
    // endTransmission() returns 0 only if a device acknowledged.
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      found++;
      Serial.print(F("  found 0x"));
      if (addr < 16) Serial.print('0');
      Serial.print(addr, HEX);
      if      (addr == 0x3C) Serial.print(F("   <- display A, the face"));
      else if (addr == 0x3D) Serial.print(F("   <- display B, the info panel"));
      Serial.println();
    }
  }

  if (found == 0) {
    Serial.println(F("  nothing found."));
    Serial.println(F("  check: SDA on D2, SCL on D1, VCC on 3.3V, GND on GND."));
  } else {
    Serial.print(F("  "));
    Serial.print(found);
    Serial.println(F(" device(s) total."));
  }

  delay(3000);                // throwaway tool, so a delay is acceptable
}
