/*
 * read_uid.ino - a throwaway helper sketch. It does ONE job: print the
 * UID of every card you tap, in exactly the format Desky's config.h wants.
 *
 * Use this before the main sketch is finished:
 *   1. Flash this sketch to the NodeMCU.
 *   2. Open the serial monitor at 115200 baud.
 *   3. Tap each of your 4 cards, one at a time.
 *   4. Copy each printed UID into the CARD_UID_* lines in desky/config.h.
 *
 * Only the OLED is unused here, so the display may stay blank. That is
 * expected - this sketch talks to the serial monitor only.
 *
 * Wiring is the same as the main project:
 *   RC522 SDA(SS) -> D4 / GPIO2   (blue)
 *   RC522 SCK     -> D5 / GPIO14  (white)
 *   RC522 MISO    -> D6 / GPIO12  (grey)
 *   RC522 MOSI    -> D7 / GPIO13  (pink)
 *   RC522 RST     -> 3.3V         (tied high, no GPIO used)
 *   RC522 3.3V    -> 3.3V rail    -- never 5V, it will kill the module
 *   RC522 GND     -> GND rail
 *   RC522 IRQ     -> not connected
 */
#include <SPI.h>
#include <MFRC522.h>

#define PIN_RC522_SS   2            // D4
#define PIN_RC522_RST  UINT8_MAX    // RST is tied to 3.3V, so "unused"

MFRC522 reader(PIN_RC522_SS, PIN_RC522_RST);

// Last UID seen, so holding a card still on the reader does not spam.
static String lastUid = "";
static uint32_t lastSeenAt = 0;

// Turn the raw UID bytes into "DE AD BE EF" - upper case, space separated.
// This is the exact text format that config.h compares against.
static String uidToString(const MFRC522::Uid &uid) {
  String s = "";
  for (byte i = 0; i < uid.size; i++) {
    if (i) s += ' ';
    if (uid.uidByte[i] < 0x10) s += '0';
    s += String(uid.uidByte[i], HEX);
  }
  s.toUpperCase();
  return s;
}

void setup() {
  Serial.begin(115200);
  delay(200);                  // fine in this throwaway tool, not in Desky
  Serial.println(F("\n\n--- Desky card UID reader ---"));

  SPI.begin();                 // ESP8266 default SPI pins are already 14/12/13
  reader.PCD_Init();

  // A quick self test. If this prints 0x00 or 0xFF the RC522 is not wired
  // up correctly - check 3.3V, GND, and the SS/SCK/MISO/MOSI lines.
  byte version = reader.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print(F("RC522 firmware version: 0x"));
  Serial.println(version, HEX);
  if (version == 0x00 || version == 0xFF)
    Serial.println(F("** reader not responding - check the wiring **"));
  else
    Serial.println(F("Reader OK. Tap a card."));
}

void loop() {
  if (!reader.PICC_IsNewCardPresent()) return;
  if (!reader.PICC_ReadCardSerial())   return;

  String uid = uidToString(reader.uid);

  // Ignore the same card if it is re-read within 2 seconds.
  if (uid != lastUid || millis() - lastSeenAt > 2000) {
    Serial.print(F("UID: \""));
    Serial.print(uid);
    Serial.print(F("\"   <- paste this into config.h   ("));
    Serial.print(reader.uid.size);
    Serial.println(F(" bytes)"));
    lastUid    = uid;
    lastSeenAt = millis();
  }

  reader.PICC_HaltA();
}
