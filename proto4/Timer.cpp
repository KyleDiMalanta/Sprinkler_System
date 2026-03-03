#include "Timer.h"

Timer::Timer(uint32_t period_seconds)
  : period(period_seconds * 1000UL), last(0) {}

bool Timer::isDone()
{
  uint32_t now = millis();
  Serial.println(now);
  if (now - last >= period)
  {
    last = now;
    return true;
  }
  return false;
}
