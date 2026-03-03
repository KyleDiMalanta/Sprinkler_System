#pragma once
#include <Arduino.h>

class Joystick {
public:
  Joystick(int xPin, int yPin, int buttonPin,
           int deadzone,
           unsigned long debounceMs);

  // Digital moves
  bool moveUp();
  bool moveDown();
  bool moveLeft();
  bool moveRight();

  // Button Logic
  bool justPressed();

protected:
  int Ax, Ay, Btn;
  int deadzone;

  unsigned long debounceTolerance;

  bool wasPressed = false;           // last loop's pressed state
  unsigned long pressStartTime = 0;  // when press began
  bool armed = false;               // true once held >= debounceTolerance

  // Shared helpers for subclasses
  bool axisBool(int pin, bool wantPositive);
  int  axisStep(int pin, bool wantPositive, int divisions);
};

class JoystickScaled : public Joystick {
public:
  JoystickScaled(int xPin, int yPin, int buttonPin,
                 int deadzone,
                 unsigned long debounceMs,
                 int divisions);

  // Only available on scaled joystick
  int moveUpScaled();
  int moveDownScaled();
  int moveLeftScaled();
  int moveRightScaled();

private:
  int divisions;
};
