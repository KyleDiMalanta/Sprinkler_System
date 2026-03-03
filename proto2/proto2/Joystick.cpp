#include "Joystick.h"

// -------- Joystick (base) --------
Joystick::Joystick(int xPin, int yPin, int buttonPin,
                   int deadzone,
                   unsigned long debounceMs)
  : Ax(xPin),
    Ay(yPin),
    Btn(buttonPin),
    deadzone(deadzone),
    debounceTolerance(debounceMs),

    // Button state (bare minimum)
    wasPressed(false),
    pressStartTime(0),
    armed(false)
{
  pinMode(Btn, INPUT_PULLUP);
}


bool Joystick::axisBool(int pin, bool wantPositive)
{
  int v = analogRead(pin) - 512;
  if (wantPositive && v <= 0) return false;
  if (!wantPositive && v >= 0) return false;
  return abs(v) > deadzone;
}

int Joystick::axisStep(int pin, bool wantPositive, int divisions)
{
  int v = analogRead(pin) - 512;

  if (wantPositive && v <= 0) return 0;
  if (!wantPositive && v >= 0) return 0;

  int mag = abs(v);
  if (mag <= deadzone) return 0;

  const int maxMag = 512;
  if (mag > maxMag) mag = maxMag;

  // Map (deadzone..maxMag) -> (1..divisions)
  long num = (long)(mag - deadzone) * divisions;
  long den = (long)(maxMag - deadzone);

  int step = (int)((num + den - 1) / den); // ceil
  if (step < 1) step = 1;
  if (step > divisions) step = divisions;
  return step;
}

bool Joystick::moveUp()    { return axisBool(Ay, true);  }
bool Joystick::moveDown()  { return axisBool(Ay, false); }
bool Joystick::moveRight() { return axisBool(Ax, true);  }
bool Joystick::moveLeft()  { return axisBool(Ax, false); }

bool Joystick::justPressed()
{
  bool pressed = (digitalRead(Btn) == LOW);  // LOW = pressed
  unsigned long now = millis();

  // Button just went DOWN
  if (pressed && !wasPressed)
  {
    pressStartTime = now;
    armed = false;   // must re-qualify every press
  }

  // While being held, qualify after debounce time
  if (pressed && !armed)
  {
    if (now - pressStartTime >= debounceTolerance)
    {
      armed = true;
    }
  }

  bool fired = false;

  // Button just went UP → fire ONLY here
  if (!pressed && wasPressed)
  {
    if (armed)
    {
      fired = true;
    }
    armed = false;    // reset for next press
  }

  wasPressed = pressed;
  return fired;
}


// -------- JoystickScaled (derived) --------
JoystickScaled::JoystickScaled(int xPin, int yPin, int buttonPin,
                               int deadzone,
                               unsigned long debounceMs,
                               int divisions)
  : Joystick(xPin, yPin, buttonPin, deadzone, debounceMs),
    divisions(divisions)
{}

int JoystickScaled::moveUpScaled()    { return axisStep(Ay, true,  divisions); }
int JoystickScaled::moveDownScaled()  { return axisStep(Ay, false, divisions); }
int JoystickScaled::moveRightScaled() { return axisStep(Ax, true,  divisions); }
int JoystickScaled::moveLeftScaled()  { return axisStep(Ax, false, divisions); }
