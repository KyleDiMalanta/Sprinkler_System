#pragma once
#include <Arduino.h>

class Timer {
public:
  Timer(uint32_t period_minutes);
  bool isDone();

private:
  uint32_t period;
  uint32_t last;
};

