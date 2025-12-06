#include <SPI.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

#define PIN_SCK   18
#define PIN_MOSI  23
#define PIN_MISO  19   // GDO2 !
#define PIN_CS    5
#define PIN_GDO0  4

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("Initializing CC1101 (HW-863)...");

  // Tell library which pins ESP32 uses
  ELECHOUSE_cc1101.setSpiPin(PIN_SCK, PIN_MOSI, PIN_MISO, PIN_CS);
  ELECHOUSE_cc1101.setGDO(PIN_GDO0, 0);

  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  SPI.setFrequency(200000);  // slow for stability

  // Wake-up pulse
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  delay(5);
  digitalWrite(PIN_CS, LOW);
  delay(10);
  digitalWrite(PIN_CS, HIGH);
  delay(10);

  if (ELECHOUSE_cc1101.getCC1101()) {
    Serial.println("✔ CC1101 detected!");
  } else {
    Serial.println("✘ CC1101 NOT detected. Check wiring.");
  }

  ELECHOUSE_cc1101.Init();
}

void loop() {}
