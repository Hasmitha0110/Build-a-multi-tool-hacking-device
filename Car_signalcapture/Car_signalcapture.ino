#include <ELECHOUSE_CC1101_SRC_DRV.h>

#define PIN_SCK   18
#define PIN_MISO  19
#define PIN_MOSI  23
#define PIN_CS    5
#define PIN_GDO0  25
#define PIN_GDO2  26

float freqs[] = {
  300.0, 315.0, 330.0, 360.0, 380.0,
  400.0, 433.92, 450.0, 470.0
};

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== CC1101 Auto Frequency Scanner ===");

  ELECHOUSE_cc1101.setSpiPin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setGDO(PIN_GDO0, PIN_GDO2);
}

void loop() {
  for (int i = 0; i < sizeof(freqs) / sizeof(freqs[0]); i++) {
    float f = freqs[i];

    ELECHOUSE_cc1101.setMHZ(f);
    ELECHOUSE_cc1101.SetRx();

    Serial.print("Scanning ");
    Serial.print(f);
    Serial.println(" MHz...");

    int noise = 0;

    for (int j = 0; j < 30; j++) {
      int rssi = ELECHOUSE_cc1101.getRssi();
      if (rssi > -60) {  // signal detected strongly
        Serial.println("==================================");
        Serial.print("SIGNAL DETECTED at ");
        Serial.print(f);
        Serial.println(" MHz!");
        Serial.print("RSSI raw: ");
        Serial.println(rssi);
        Serial.println("==================================");
        delay(5000);
      }
      delay(60);
    }

    delay(200);
  }
}
