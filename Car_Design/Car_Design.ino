#define D1_PIN 2
#define D2_PIN 3
#define D3_PIN 4
#define D4_PIN 5
#define VT_PIN 6

#define LED_LOCK 8
#define LED_UNLOCK 9
#define LED_TRUNK 10
#define LED_ALARM 11

void setup() {
  pinMode(D1_PIN, INPUT);
  pinMode(D2_PIN, INPUT);
  pinMode(D3_PIN, INPUT);
  pinMode(D4_PIN, INPUT);
  pinMode(VT_PIN, INPUT);

  pinMode(LED_LOCK, OUTPUT);
  pinMode(LED_UNLOCK, OUTPUT);
  pinMode(LED_TRUNK, OUTPUT);
  pinMode(LED_ALARM, OUTPUT);

  digitalWrite(LED_LOCK, LOW);
  digitalWrite(LED_UNLOCK, LOW);
  digitalWrite(LED_TRUNK, LOW);
  digitalWrite(LED_ALARM, LOW);

  Serial.begin(9600);
}

void loop() {
  // Read signals
  int vt = digitalRead(VT_PIN);
  int d1 = digitalRead(D1_PIN);
  int d2 = digitalRead(D2_PIN);
  int d3 = digitalRead(D3_PIN);
  int d4 = digitalRead(D4_PIN);

  // If signal detected
  if (vt == HIGH) {
    if (d1 == HIGH) {
      digitalWrite(LED_LOCK, HIGH);
      Serial.println("LOCK");
      delay(500);
      digitalWrite(LED_LOCK, LOW);
    }
    if (d2 == HIGH) {
      digitalWrite(LED_UNLOCK, HIGH);
      Serial.println("UNLOCK");
      delay(500);
      digitalWrite(LED_UNLOCK, LOW);
    }
    if (d3 == HIGH) {
      digitalWrite(LED_TRUNK, HIGH);
      Serial.println("TRUNK");
      delay(500);
      digitalWrite(LED_TRUNK, LOW);
    }
    if (d4 == HIGH) {
      digitalWrite(LED_ALARM, HIGH);
      Serial.println("ALARM");
      delay(500);
      digitalWrite(LED_ALARM, LOW);
    }
  }
}