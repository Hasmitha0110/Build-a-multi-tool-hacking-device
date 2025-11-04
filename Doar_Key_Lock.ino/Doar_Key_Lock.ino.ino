#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN 10
#define RST_PIN 9
#define GREEN_LED 7
#define RED_LED 6
#define BUZZER 5

MFRC522 mfrc522(SS_PIN, RST_PIN);

// Authorized UIDs - BOTH physical cards AND PN532 emulated cards
byte authorizedUIDs[][4] = {
  {0x29, 0xBD, 0x1B, 0x06}, // Physical card format
  {0x08, 0xBD, 0x1B, 0x06}  // PN532 emulated format
};

const int numAuthorizedCards = 2;

void setup() {
  Serial.begin(9600);
  SPI.begin();
  mfrc522.PCD_Init();
  
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
  
  Serial.println("🚪 UNIVERSAL RFID Door Lock");
  Serial.println("============================");
  Serial.println("Supports: Physical Cards + PN532 Emulation");
  Serial.println("Authorized UIDs:");
  Serial.println("1. Physical: 29 BD 1B 06");
  Serial.println("2. PN532:    08 29 BD 1B");
  Serial.println("Waiting for cards...");
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
  
  // Show detailed card info
  Serial.print("Card Detected - UID:");
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
  }
  Serial.print(" | Size: ");
  Serial.print(mfrc522.uid.size);
  Serial.print(" | Type: ");
  
  // Detect card type
  if (mfrc522.uid.size == 4 && mfrc522.uid.uidByte[0] == 0x08) {
    Serial.print("PN532 Emulated");
  } else if (mfrc522.uid.size == 4) {
    Serial.print("Physical Card");
  } else {
    Serial.print("Unknown");
  }
  Serial.println();
  
  // Check if authorized
  if (isAuthorizedCard()) {
    grantAccess();
  } else {
    denyAccess();
  }
  
  delay(1000);
}

bool isAuthorizedCard() {
  // Must be 4-byte UID
  if (mfrc522.uid.size != 4) {
    return false;
  }
  
  // Check against all authorized UIDs
  for (int card = 0; card < numAuthorizedCards; card++) {
    bool match = true;
    for (byte i = 0; i < 4; i++) {
      if (mfrc522.uid.uidByte[i] != authorizedUIDs[card][i]) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  
  return false;
}

void grantAccess() {
  Serial.println("✅ ACCESS GRANTED - Door Unlocked!");
  
  // Visual and audio feedback
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED, LOW);
  beep(200, 2);
  
  // Simulate door unlock (5 seconds)
  Serial.println("🔓 Door unlocked for 5 seconds...");
  delay(5000);
  
  // Lock door again
  Serial.println("🔒 Door locked");
  digitalWrite(GREEN_LED, LOW);
  Serial.println("Waiting for next card...");
}

void denyAccess() {
  Serial.println("❌ ACCESS DENIED - Unauthorized Card!");
  
  // Visual and audio feedback
  digitalWrite(RED_LED, HIGH);
  digitalWrite(GREEN_LED, LOW);
  beep(800, 1);
  
  delay(3000);
  digitalWrite(RED_LED, LOW);
  Serial.println("Waiting for authorized card...");
}

void beep(int duration, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(duration);
    digitalWrite(BUZZER, LOW);
    if (i < times - 1) delay(200);
  }
}