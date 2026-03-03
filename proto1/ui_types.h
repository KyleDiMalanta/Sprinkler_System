#pragma once

struct InputEvent {
  int dx;      // -1 left, +1 right, 0 none
  int dy;      // -1 up, +1 down, 0 none
  bool press;  // true only on NEW press event
};
