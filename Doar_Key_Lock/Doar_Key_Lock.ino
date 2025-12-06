// Redesigned Door Lock Sketch - Matches final wiring (NO KEYPAD)
// Components & Pins (must match your wiring):
// RC522 (3.3V):  SDA(SS)=D10, SCK=D13, MOSI=D11, MISO=D12, RST=D9
// I2C LCD: SDA=A4, SCL=A5  (LiquidCrystal_I2C default address 0x27 — change if needed)
// RELAY IN: D7  (switches 12V to solenoid via COM-NO)
// BUZZER (active): D3
// GREEN LED (access granted): A0 (used as digital pin)
// RED LED (access denied): A1 (used as digital pin)
// Note: Arduino GND must be common with 12V adapter negative

#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --------- PIN DEFINITIONS (final layout) ----------
#define SS_PIN       10    // RC522 SDA (SS)
#define RST_PIN      9     // RC522 RST
#define RELAY_PIN    7     // Relay IN (controls solenoid's 12V via COM/NO)
#define BUZZER_PIN   3     // Active buzzer +
#define GREEN_LED_PIN A0   // Green LED anode via 220Ω (digital)
#define RED_LED_PIN   A1   // Red LED anode via 220Ω (digital)
// --------------------------------------------------

// If your relay module is active LOW change this to LOW. If it is active HIGH, set to HIGH.
// Many single-channel relay modules (with optocoupler/jumpers) are active LOW by default.
const uint8_t RELAY_ACTIVE_STATE = HIGH;
const uint8_t RELAY_INACTIVE_STATE = (RELAY_ACTIVE_STATE == LOW) ? HIGH : LOW;

// LCD - change address to 0x3F if your module uses that address
LiquidCrystal_I2C lcd(0x27, 16, 2);

// RFID object
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Authorized UIDs (4-byte format). Edit or add UIDs as needed.
// Keep them as HEX bytes, e.g., {0x29, 0xBD, 0x1B, 0x06}
byte authorizedUIDs[][4] = {
  {0x29, 0xBD, 0x1B, 0x06}, // Physical card
  {0x08, 0xBD, 0x1B, 0x06}  // Example PN532-emulated format (adjust if needed)
};
const uint8_t numAuthorizedCards = sizeof(authorizedUIDs) / 4;

// Timing
const unsigned long UNLOCK_DURATION_MS = 5000UL; // how long the door stays unlocked

// ----------------- Utility functions -----------------
void beep(unsigned int durationMs, unsigned int times) {
  for (unsigned int i = 0; i < times; ++i) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(durationMs);
    digitalWrite(BUZZER_PIN, LOW);
    if (i + 1 < times) delay(150);
  }
}

void activateRelay(bool activate) {
  digitalWrite(RELAY_PIN, activate ? RELAY_ACTIVE_STATE : RELAY_INACTIVE_STATE);
}

void showLCDStatus(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(line1);
  lcd.setCursor(0,1);
  lcd.print(line2);
}

// Compare current read UID to the authorized list
bool isAuthorizedCard() {
  if (mfrc522.uid.size != 4) return false; // we only accept 4-byte UIDs here

  for (uint8_t card = 0; card < numAuthorizedCards; ++card) {
    bool match = true;
    for (byte i = 0; i < 4; ++i) {
      if (mfrc522.uid.uidByte[i] != authorizedUIDs[card][i]) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

// Called when access is granted
void grantAccess() {
  Serial.println("✅ ACCESS GRANTED - Door Unlocked!");
  showLCDStatus("ACCESS GRANTED", "Door unlocked...");

  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(RED_LED_PIN, LOW);
  beep(120, 2);             // short double beep

  activateRelay(true);      // energize relay (unlock) - check RELAY_ACTIVE_STATE above

  // Keep door unlocked for defined duration
  unsigned long start = millis();
  while (millis() - start < UNLOCK_DURATION_MS) {
    // Could add non-blocking tasks here
    delay(50);
  }

  // Lock door again
  activateRelay(false);
  digitalWrite(GREEN_LED_PIN, LOW);
  Serial.println("🔒 Door locked");
  showLCDStatus("Door locked", "Waiting for card...");
}

// Called when access is denied
void denyAccess() {
  Serial.println("❌ ACCESS DENIED - Unauthorized Card!");
  showLCDStatus("ACCESS DENIED", "Unauthorized");

  digitalWrite(RED_LED_PIN, HIGH);
  digitalWrite(GREEN_LED_PIN, LOW);
  beep(400, 2);   // longer beep pattern for denied

  delay(1200);
  digitalWrite(RED_LED_PIN, LOW);
  showLCDStatus("Waiting for card...", "");
}

// Print UID in HEX nicely
void printUID() {
  Serial.print("Card Detected - UID:");
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) Serial.print(" 0");
    else Serial.print(" ");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
  }
  Serial.print("  | Size: ");
  Serial.print(mfrc522.uid.size);
  Serial.print(" | Type: ");
  if (mfrc522.uid.size == 4 && mfrc522.uid.uidByte[0] == 0x08) {
    Serial.print("PN532 Emulated");
  } else if (mfrc522.uid.size == 4) {
    Serial.print("Physical Card");
  } else {
    Serial.print("Unknown");
  }
  Serial.println();
}

// ----------------- Arduino setup & loop -----------------
void setup() {
  Serial.begin(9600);
  SPI.begin();           // Init SPI bus for RC522
  mfrc522.PCD_Init();

  // Initialize LCD
  Wire.begin();
  lcd.init();
  lcd.backlight();

  // Pin modes
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(GREEN_LED_PIN, OUTPUT);
  digitalWrite(GREEN_LED_PIN, LOW);

  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, LOW);

  pinMode(RELAY_PIN, OUTPUT);
  activateRelay(false); // ensure relay is off (locked) at start

  // Serial & LCD header
  Serial.println(F("🚪 Redesigned RFID Door Lock (Final Pin Layout)"));
  Serial.println(F("============================================="));
  Serial.print(F("Authorized UIDs: "));
  Serial.println(numAuthorizedCards);
  Serial.println(F("Waiting for cards..."));

  showLCDStatus("System Ready", "Waiting for card...");
  delay(500);
}

void loop() {
  // Look for new cards
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  // Select one of the cards
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  // We have a card — show info
  printUID();

  // Check authorization
  if (isAuthorizedCard()) {
    grantAccess();
  } else {
    denyAccess();
  }

  // Halt PICC and stop encryption on PCD for next card
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  // small debounce / safety delay
  delay(300);
}
