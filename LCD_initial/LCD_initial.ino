#include <LiquidCrystal.h>
LiquidCrystal lcd(10, 9, 8, 7, 6, 5);

const int xPin = A6;
const int yPin = A7;
const int swPin = 4;

const int COLS = 16;
const int ROWS = 2;

// Tuning knobs
const int DEADZONE = 120;          // bigger = less jitter (try 80–200)
const unsigned long MOVE_MS = 120; // smaller = faster cursor

int cx = 0;   // cursor column
int cy = 0;   // cursor row

unsigned long lastMove = 0;

void setup() {
  Serial.begin(9600);

  lcd.begin(COLS, ROWS);
  lcd.clear();
  //lcd.print("Use joystick");

  pinMode(swPin, INPUT_PULLUP);
  lcd.cursor();      // show underline cursor
  lcd.blink();       // optional: blinking cursor
}

void loop() {
  int xVal = analogRead(xPin);   // 0..1023
  int yVal = analogRead(yPin);   // 0..1023
  int swVal = digitalRead(swPin);

  // Debug (optional)
  Serial.print("X: "); Serial.print(xVal);
  Serial.print(" Y: "); Serial.print(yVal);
  Serial.print(" SW: "); Serial.println(swVal);

  unsigned long now = millis();
  if (now - lastMove >= MOVE_MS) {
    bool moved = false;

    // Horizontal move
    if (xVal < 512 - DEADZONE) {           // left
      if (cx > 0) cx--;
      moved = true;
    } else if (xVal > 512 + DEADZONE) {    // right
      if (cx < COLS - 1) cx++;
      moved = true;
    }

    // Vertical move (fixed inversion)
    if (yVal > 512 + DEADZONE) {        // up
      if (cy > 0) cy--;
      moved = true;
    } 
    else if (yVal < 512 - DEADZONE) {   // down
      if (cy < ROWS - 1) cy++;
      moved = true;
    }


    if (moved) {
      lcd.setCursor(cx, cy);
      lastMove = now;
    }
  }

  // Optional: press joystick to "click" (example: toggle blink)
  if (swVal == LOW) {
    lcd.noBlink();
    delay(200); // crude debounce
    lcd.blink();
  }
}

