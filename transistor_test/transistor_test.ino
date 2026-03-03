int p_out = 10;

void setup() {
  // put your setup code here, to run once:
  pinMode(p_out, OUTPUT);
  

}

void loop() {
  // put your main code here, to run repeatedly:
  digitalWrite(p_out, HIGH);
  delay(2000);
  digitalWrite(p_out, LOW);
  delay(2000);
}
