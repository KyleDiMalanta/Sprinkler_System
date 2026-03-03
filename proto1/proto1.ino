#include "ui_types.h"
#include <LiquidCrystal.h>
#include <EEPROM.h>

// ---------------- LCD ----------------
LiquidCrystal lcd(10, 9, 8, 7, 6, 5);
const int COLS = 16;
const int ROWS = 2;

// ---------------- Joystick ----------------
// NOTE: A6/A7 exist on Nano/Pro Mini. On Uno use A0/A1 instead.
const int X_in = A6;
const int Y_in = A7;
const int B_in = 4;   // active-low

const int DEADZONE = 150;
const unsigned long MOVE_MS = 160;
const unsigned long DEBOUNCE_MS = 50;

// ---------------- Plant data (NA support) ----------------
const uint16_t NA_U16 = 0xFFFF;
const uint8_t MAX_PLANTS = 6;

struct Plant {
  uint16_t periodDays;  // NA_U16 = NA
  uint16_t amountMl;    // NA_U16 = NA
  Plant() : periodDays(NA_U16), amountMl(NA_U16) {}
};

Plant plants[MAX_PLANTS];
uint8_t selectedPlant = 0;

inline bool isNA(uint16_t v) { return v == NA_U16; }

void lcdPrintU16orNA(uint16_t v) {
  if (isNA(v)) lcd.print("NA");
  else lcd.print(v);
}

// ---------------- EEPROM ----------------
const uint16_t EEPROM_MAGIC = 0xBEEF;

struct PlantStore {
  uint16_t magic;
  Plant plants[MAX_PLANTS];
};

void savePlantsToEEPROM() {
  PlantStore store;
  store.magic = EEPROM_MAGIC;
  for (uint8_t i = 0; i < MAX_PLANTS; i++) store.plants[i] = plants[i];

  const uint8_t* p = (const uint8_t*)&store;
  for (unsigned int i = 0; i < sizeof(PlantStore); i++) {
    EEPROM.update(i, p[i]);
  }
}

bool loadPlantsFromEEPROM() {
  PlantStore store;
  EEPROM.get(0, store);
  if (store.magic != EEPROM_MAGIC) return false;

  for (uint8_t i = 0; i < MAX_PLANTS; i++) plants[i] = store.plants[i];
  return true;
}

// ---------------- Input debounce state ----------------
int g_lastBReading = HIGH;
int g_stableBState = HIGH;
unsigned long g_lastBounce = 0;
unsigned long g_lastMove = 0;

void initInput() {
  while (digitalRead(B_in) == LOW) delay(10);
  int b = digitalRead(B_in);
  g_lastBReading = b;
  g_stableBState = b;
  g_lastBounce = millis();
  g_lastMove = millis();
}

InputEvent readInput() {
  InputEvent e{0, 0, false};
  unsigned long now = millis();

  int xVal = analogRead(X_in);
  int yVal = analogRead(Y_in);
  int bVal = digitalRead(B_in);

  int dx = 0, dy = 0;
  if (xVal > 512 + DEADZONE) dx = +1;
  else if (xVal < 512 - DEADZONE) dx = -1;

  // swap if inverted
  if (yVal > 512 + DEADZONE) dy = +1;      // down
  else if (yVal < 512 - DEADZONE) dy = -1; // up

  if ((dx || dy) && (now - g_lastMove >= MOVE_MS)) {
    e.dx = dx;
    e.dy = dy;
    g_lastMove = now;
  }

  // button debounce + edge (HIGH -> LOW)
  if (bVal != g_lastBReading) {
    g_lastBounce = now;
    g_lastBReading = bVal;
  }
  if ((now - g_lastBounce) > DEBOUNCE_MS) {
    if (g_stableBState != bVal) {
      g_stableBState = bVal;
      if (g_stableBState == LOW) e.press = true;
    }
  }

  return e;
}

// ---------------- UI state ----------------
enum Screen { HOME, EDIT_PLANT, HELP_SCREEN };
Screen screen = HOME;

enum HomeTarget { P1, P2, P3, P4, P5, P6, HOME_HELP };
HomeTarget homeSel = P1;

// Selection inside EDIT_PLANT
enum EditTarget { EDIT_PERIOD, EDIT_AMOUNT, EDIT_BACK };
EditTarget editSel = EDIT_PERIOD;

// Action toggle shown on the selected row (when not dirty and not editing)
enum RowAction { ACTION_EDIT, ACTION_CLEAR };
RowAction rowAction = ACTION_EDIT;

bool editing = false;

// Unsaved changes flag (edit or clear sets this; Done clears it)
bool dirty = false;

// Clear ONLY the selected FIELD in RAM (EEPROM updates ONLY on Done)
void clearSelectedFieldRAM() {
  if (editSel == EDIT_PERIOD) {
    plants[selectedPlant].periodDays = NA_U16;
  } else if (editSel == EDIT_AMOUNT) {
    plants[selectedPlant].amountMl = NA_U16;
  }
}

// ---------------- Drawing ----------------
void drawHome() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Edit a plant");

  lcd.setCursor(0, 1);
  lcd.print("1 2 3 4 5 6");

  lcd.setCursor(12, 1);
  lcd.print("Help");

  uint8_t col = (homeSel <= P6) ? (uint8_t)homeSel * 2 : 12;
  lcd.setCursor(col, 1);
  lcd.cursor();
  lcd.blink();
}

// We place the 4-char label at col 11..14 so col 15 can be a visible back arrow.
const uint8_t LABEL_COL = 11;
const uint8_t BACK_COL  = 15;

static void printRowLabel(bool selectedRow) {
  // If there are pending changes and NOT currently editing, show Done on selected row
  if (!editing && dirty && selectedRow) {
    lcd.print("Done");
    return;
  }

  // If actively editing this row, show Done (exit editing)
  if (editing && selectedRow) {
    lcd.print("Done");
    return;
  }

  // Normal: show Edit / Clr on the selected row; show Edit on the other
  if (selectedRow) {
    lcd.print((rowAction == ACTION_CLEAR) ? "Clr " : "Edit");
  } else {
    lcd.print("Edit");
  }
}

void drawEditPlant() {
  lcd.clear();

  // Top-right back arrow always visible
  lcd.setCursor(BACK_COL, 0);
  lcd.print("<");

  // Row 0: Period
  lcd.setCursor(0, 0);
  lcd.print("P");
  lcd.print(selectedPlant + 1);
  lcd.print(" Per:");
  lcdPrintU16orNA(plants[selectedPlant].periodDays);

  lcd.setCursor(LABEL_COL, 0);
  printRowLabel(editSel == EDIT_PERIOD);

  // Row 1: Amount
  lcd.setCursor(0, 1);
  lcd.print("Amt:");
  lcdPrintU16orNA(plants[selectedPlant].amountMl);

  lcd.setCursor(LABEL_COL, 1);
  printRowLabel(editSel == EDIT_AMOUNT);

  // Cursor placement
  uint8_t col = LABEL_COL, row = 0;
  if (editSel == EDIT_PERIOD) { col = LABEL_COL; row = 0; }
  else if (editSel == EDIT_AMOUNT) { col = LABEL_COL; row = 1; }
  else { col = BACK_COL; row = 0; } // back arrow

  lcd.setCursor(col, row);
  lcd.cursor();
  lcd.blink();
}

void drawHelp() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Help: stick+btn");
  lcd.setCursor(0, 1);
  lcd.print("Press=back");
  lcd.setCursor(BACK_COL, 0);
  lcd.print("<");
  lcd.setCursor(BACK_COL, 0);
  lcd.cursor();
  lcd.blink();
}

void ensureDefaultsIfNA() {
  if (editSel == EDIT_PERIOD && isNA(plants[selectedPlant].periodDays))
    plants[selectedPlant].periodDays = 7;
  if (editSel == EDIT_AMOUNT && isNA(plants[selectedPlant].amountMl))
    plants[selectedPlant].amountMl = 200;
}

// ---------------- Handlers ----------------
void handleHome(const InputEvent& e) {
  if (e.dx != 0) {
    int idx = (int)homeSel + e.dx;
    if (idx < 0) idx = (int)HOME_HELP;
    if (idx > (int)HOME_HELP) idx = 0;
    homeSel = (HomeTarget)idx;
    drawHome();
  }

  if (e.press) {
    if (homeSel == HOME_HELP) {
      screen = HELP_SCREEN;
      drawHelp();
    } else {
      selectedPlant = (uint8_t)homeSel;
      screen = EDIT_PLANT;

      editSel = EDIT_PERIOD;
      editing = false;
      dirty = false;
      rowAction = ACTION_EDIT;

      drawEditPlant();
    }
  }
}

void handleEditPlant(const InputEvent& e) {
  if (!editing) {
    // Up/Down cycles: PERIOD -> AMOUNT -> BACK -> PERIOD ...
    if (e.dy != 0) {
      if (editSel == EDIT_PERIOD) editSel = EDIT_AMOUNT;
      else if (editSel == EDIT_AMOUNT) editSel = EDIT_BACK;
      else editSel = EDIT_PERIOD;

      if (!dirty) rowAction = ACTION_EDIT;
      drawEditPlant();
    }

    // Left/Right toggles action when on a row AND not dirty
    if (e.dx != 0 && !dirty && editSel != EDIT_BACK) {
      rowAction = (rowAction == ACTION_EDIT) ? ACTION_CLEAR : ACTION_EDIT;
      drawEditPlant();
    }

    if (e.press) {
      if (editSel == EDIT_BACK) {
        // Leave without saving (EEPROM only writes on Done)
        screen = HOME;
        drawHome();
        return;
      }

      // If we have unsaved changes, Done confirms/save to EEPROM
      if (dirty) {
        savePlantsToEEPROM();
        dirty = false;
        rowAction = ACTION_EDIT;
        drawEditPlant();
        return;
      }

      // Otherwise, either Clear field or enter Edit mode
      if (rowAction == ACTION_CLEAR) {
        clearSelectedFieldRAM(); // ONLY clears selected row's value
        dirty = true;            // Done now appears on selected row
        rowAction = ACTION_EDIT; // reset after action
        drawEditPlant();
        return;
      }

      // Enter editing mode
      editing = true;
      ensureDefaultsIfNA();
      drawEditPlant();
    }
  } else {
    // editing mode: left/right changes values
    if (e.dx != 0) {
      if (editSel == EDIT_PERIOD) {
        int v = (int)plants[selectedPlant].periodDays + e.dx;
        if (v < 1) v = 1;
        plants[selectedPlant].periodDays = (uint16_t)v;
      } else if (editSel == EDIT_AMOUNT) {
        int v = (int)plants[selectedPlant].amountMl + (10 * e.dx);
        if (v < 0) v = 0;
        plants[selectedPlant].amountMl = (uint16_t)v;
      }
      dirty = true;
      drawEditPlant();
    }

    // press exits editing mode (shows Done, but doesn't save yet)
    if (e.press) {
      editing = false;
      dirty = true;
      rowAction = ACTION_EDIT;
      drawEditPlant();
    }
  }
}

void handleHelpScreen(const InputEvent& e) {
  if (e.press) {
    screen = HOME;
    drawHome();
  }
}

// ---------------- setup/loop ----------------
void setup() {
  pinMode(B_in, INPUT_PULLUP);
  lcd.begin(COLS, ROWS);

  initInput();
  loadPlantsFromEEPROM();

  screen = HOME;
  homeSel = P1;
  editing = false;
  dirty = false;
  rowAction = ACTION_EDIT;

  drawHome();
}

void loop() {
  InputEvent e = readInput();
  if (e.dx == 0 && e.dy == 0 && !e.press) return;

  switch (screen) {
    case HOME:        handleHome(e); break;
    case EDIT_PLANT:  handleEditPlant(e); break;
    case HELP_SCREEN: handleHelpScreen(e); break;
  }
}
