// int pump_out = A5;
// int plant1 = A0;
// int plant2 = A1;
int pump = A2;
// int plant4 = A3;
// int plant5 = A4;

void setup() {
  // pinMode(pump_out, OUTPUT);   // A0 used as digital output
  // pinMode(plant1, OUTPUT);
  // pinMode(plant2, OUTPUT);
  // pinMode(plant3, OUTPUT);
  // pinMode(plant4, OUTPUT);
  // pinMode(plant5, OUTPUT);
  pinMode(pump, OUTPUT);
}

void loop() {
  digitalWrite(pump, HIGH);
  delay(3000);
  digitalWrite(pump, LOW);
  delay(3000);
  // digitalWrite(pump_out, HIGH);
  // digitalWrite(plant1, HIGH);
  // delay(3000);

  // digitalWrite(plant1, LOW);
  // digitalWrite(plant2, HIGH);
  // delay(3000);

  // digitalWrite(plant2, LOW);
  // digitalWrite(plant3, HIGH);
  // delay(3000);

  // digitalWrite(plant3, LOW);
  // digitalWrite(plant4, HIGH);
  // delay(3000);

  // digitalWrite(plant4, LOW);
  // digitalWrite(plant5, HIGH);
  // delay(3000);

  // digitalWrite(plant5, LOW);
  // digitalWrite(pump_out, LOW);   // turn OFF
  // delay(3000);                   // optional: wait 5 seconds before repeating
}

