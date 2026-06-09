#define D1_PIN 2
#define D2_PIN 3
#define D3_PIN 4
#define D4_PIN 5
#define VT_PIN 6

#define LED_LOCK 8
#define LED_TRUNK 10
#define LED_ALARM 11

// Previous states
int prev_d1 = LOW;
int prev_d2 = LOW;
int prev_d3 = LOW;
int prev_d4 = LOW;

void setup() {
  pinMode(D1_PIN, INPUT);
  pinMode(D2_PIN, INPUT);
  pinMode(D3_PIN, INPUT);
  pinMode(D4_PIN, INPUT);
  pinMode(VT_PIN, INPUT);

  pinMode(LED_LOCK, OUTPUT);
  pinMode(LED_TRUNK, OUTPUT);
  pinMode(LED_ALARM, OUTPUT);

  digitalWrite(LED_LOCK, LOW);
  digitalWrite(LED_TRUNK, LOW);
  digitalWrite(LED_ALARM, LOW);

  Serial.begin(9600);
}

void blinkLED(int pin, int times) {
  if (times == 1) {
    // Long single blink (LOCK)
    digitalWrite(pin, HIGH);
    delay(700);
    digitalWrite(pin, LOW);
    return;
  }

  // Double blink (UNLOCK)
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(200);
    digitalWrite(pin, LOW);
    delay(200);
  }
}

void loop() {
  int vt = digitalRead(VT_PIN);

  int d1 = digitalRead(D1_PIN);
  int d2 = digitalRead(D2_PIN);
  int d3 = digitalRead(D3_PIN);
  int d4 = digitalRead(D4_PIN);

  if (vt == HIGH) {

    // LOCK — one long blink
    if (d1 == HIGH && prev_d1 == LOW) {
      Serial.println("LOCK");
      blinkLED(LED_LOCK, 1);
    }

    // UNLOCK — double blink
    if (d2 == HIGH && prev_d2 == LOW) {
      Serial.println("UNLOCK");
      blinkLED(LED_LOCK, 2);
    }

    // TRUNK — blink continuously while holding button
    if (d3 == HIGH) {
      Serial.println("TRUNK HOLD");
      digitalWrite(LED_TRUNK, HIGH);
      delay(150);
      digitalWrite(LED_TRUNK, LOW);
      delay(150);
    }

    // ALARM — blink continuously while holding button
    if (d4 == HIGH) {
      Serial.println("ALARM HOLD");
      digitalWrite(LED_ALARM, HIGH);
      delay(150);
      digitalWrite(LED_ALARM, LOW);
      delay(150);
    }
  }

  // Update previous states
  prev_d1 = d1;
  prev_d2 = d2;
  prev_d3 = d3;
  prev_d4 = d4;
}
