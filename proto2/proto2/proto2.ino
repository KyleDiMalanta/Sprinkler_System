#include <LiquidCrystal.h>
#include <EEPROM.h>
#include "Joystick.h"

Joystick joy(A6, A7, 4, 200, 50);   // deadzone, debounce threshold
LiquidCrystal lcd(10, 9, 8, 7, 6, 5);

// ---------------- Global daily timer ----------------
struct GlobalDayTimer {
  static const uint32_t DAY_MS = 86400000UL; // 24h

  uint32_t lastTickMs = 0;

  void begin() { lastTickMs = millis(); }

  // returns true once per day (uptime-based)
  bool update() {
    uint32_t now = millis();
    if ((uint32_t)(now - lastTickMs) >= DAY_MS) {
      uint32_t daysPassed = (uint32_t)(now - lastTickMs) / DAY_MS;
      lastTickMs += daysPassed * DAY_MS;
      return true;
    }
    return false;
  }
};

// ---------------- Data model ----------------
struct Timer {
  // TODO: implement later
};

struct Plant {
  uint16_t periodDays = 0; // default 0
  uint16_t mlPerWater = 0; // default 0
  uint16_t daysLeft   = 0; // countdown aligned to global day tick
  Timer timer;
};

// ---------------- UI ----------------
class UI {
public:
  UI(LiquidCrystal &lcd, Joystick &joy)
    : lcd(lcd), joy(joy) {}

  void begin() {
    lcd.begin(16, 2);
    loadPlantsFromEEPROM();
    dayTimer.begin();
    lcd.clear();
    render();
  }

  void update() {
    unsigned long now = millis();

    // ---- background daily tick ----
    if (dayTimer.update()) {
      for (uint8_t i = 0; i < NUM_PLANTS; i++) {
        if (plants[i].periodDays == 0) {
          plants[i].daysLeft = 0;
        } else {
          if (plants[i].daysLeft > 0) plants[i].daysLeft--;
          // If it hits 0, this is where you'd trigger pump logic later.
        }
      }
      dirty = true;
    }

    // -------- HOME --------
    if (screen == HOME) {
      if (now - lastMoveMs >= moveDelayMs) {
        if (joy.moveRight()) { cursor = (cursor + 1) % 6; lastMoveMs = now; dirty = true; }
        else if (joy.moveLeft()) { cursor = (cursor == 0 ? 5 : cursor - 1); lastMoveMs = now; dirty = true; }
      }

      if (joy.justPressed()) {
        if (cursor <= 4) {
          selectedPlant = cursor;       // 0..4
          screen = EDIT_PLANT_HOME;
          cursor = 0;                   // 0=Edit, 1=Timer, 2=Back
        } else {
          screen = HELP;
          cursor = 0;
        }
        lcd.clear();
        dirty = true;
      }
    }

    // -------- EDIT_PLANT_HOME (Edit/Timer/Back) --------
    else if (screen == EDIT_PLANT_HOME) {
      if (now - lastMoveMs >= moveDelayMs) {
        if (joy.moveRight()) { cursor = (cursor + 1) % 3; lastMoveMs = now; dirty = true; }
        else if (joy.moveLeft()) { cursor = (cursor == 0 ? 2 : cursor - 1); lastMoveMs = now; dirty = true; }
      }

      if (joy.justPressed()) {
        if (cursor == 0) {
          // Edit -> Values screen
          screen = EDIT_PLANT_VALUES;
          cursor = 0;              // 0='-', 1='+', 2=SAVE/BACK
          editRow = 0;             // 0=Days row, 1=mL row
          editingLoaded = false;   // load draft on entry
          lcd.clear();
          dirty = true;
        }
        else if (cursor == 1) {
          // Timer -> Timer screen
          screen = TIMER;
          scrollIdx = 0;
          lastScrollMs = now;
          lcd.clear();
          dirty = true;
        }
        else {
          // Back -> Home
          screen = HOME;
          cursor = selectedPlant;  // keep highlight on that plant number
          lcd.clear();
          dirty = true;
        }
      }
    }

    // -------- EDIT_PLANT_VALUES (Days/mL with - + SAVE/BACK) --------
    else if (screen == EDIT_PLANT_VALUES) {
      if (!editingLoaded) {
        loadDraftFromPlant();
        dirty = true;
      }

      // Up/Down selects row
      if (now - lastMoveMs >= moveDelayMs) {
        if (joy.moveDown()) { editRow = 1; lastMoveMs = now; dirty = true; }
        else if (joy.moveUp()) { editRow = 0; lastMoveMs = now; dirty = true; }
      }

      // Left/Right selects '-', '+', 'SAVE/BACK'
      if (now - lastMoveMs >= moveDelayMs) {
        if (joy.moveRight()) { cursor = (cursor + 1) % 3; lastMoveMs = now; dirty = true; }
        else if (joy.moveLeft()) { cursor = (cursor == 0 ? 2 : cursor - 1); lastMoveMs = now; dirty = true; }
      }

      if (joy.justPressed()) {
        if (cursor == 0) {
          // '-'
          if (editRow == 0) { if (draftDays > DAYS_MIN) draftDays--; }
          else { if (draftMl > ML_MIN) draftMl--; }
          dirty = true;
        }
        else if (cursor == 1) {
          // '+'
          if (editRow == 0) { if (draftDays < DAYS_MAX) draftDays++; }
          else { if (draftMl < ML_MAX) draftMl++; }
          dirty = true;
        }
        else { // cursor == 2 -> SAVE on top row, BACK on bottom row
          if (editRow == 0) {
          // SAVE: commit + EEPROM
          plants[selectedPlant].periodDays = draftDays;
          plants[selectedPlant].mlPerWater = draftMl;

          // Reset countdown to the new schedule
          plants[selectedPlant].daysLeft = plants[selectedPlant].periodDays;

          // If period is 0, keep countdown at 0
          if (plants[selectedPlant].periodDays == 0) {
            plants[selectedPlant].daysLeft = 0;
          }

          savePlantToEEPROM(selectedPlant);
          dirty = true;
        }

          // BACK always returns (discard happens automatically because we didn't commit)
          screen = EDIT_PLANT_HOME;
          cursor = 0;
          editingLoaded = false;
          lcd.clear();
          dirty = true;
        }
      }
    }

    // -------- TIMER screen --------
    else if (screen == TIMER) {
      if (now - lastScrollMs >= scrollDelayMs) {
        scrollIdx++;
        lastScrollMs = now;
        dirty = true;
      }

      if (joy.justPressed()) {
        screen = EDIT_PLANT_HOME;
        cursor = 1; // keep cursor on Timer option
        lcd.clear();
        dirty = true;
      }
    }

    // -------- HELP --------
    else if (screen == HELP) {
      if (joy.justPressed()) {
        screen = HOME;
        cursor = 0;
        lcd.clear();
        dirty = true;
      }
    }

    // Render throttled
    if (dirty && (now - lastRenderMs >= 80)) {
      render();
      lastRenderMs = now;
      dirty = false;
    }
  }

private:
  static const uint8_t NUM_PLANTS = 5;
  Plant plants[NUM_PLANTS];

  enum Screen : uint8_t {
    HOME,
    EDIT_PLANT_HOME,
    EDIT_PLANT_VALUES,
    TIMER,
    HELP
  };

  LiquidCrystal &lcd;
  Joystick &joy;

  Screen screen = HOME;

  uint8_t cursor = 0;         // HOME:0..5, PlantHome:0..2, Values:0..2
  uint8_t selectedPlant = 0;  // 0..4
  uint8_t editRow = 0;        // 0=Days, 1=mL

  bool dirty = true;
  unsigned long lastRenderMs = 0;

  unsigned long lastMoveMs = 0;
  const unsigned long moveDelayMs = 180;

  // Draft values (only committed on SAVE)
  uint16_t draftDays = 0;
  uint16_t draftMl   = 0;
  bool editingLoaded = false;

  // Limits
  static const uint16_t DAYS_MIN = 0;
  static const uint16_t DAYS_MAX = 99;
  static const uint16_t ML_MIN   = 0;
  static const uint16_t ML_MAX   = 999;

  // EEPROM layout: days (2) + ml (2) + daysLeft (2) = 6 bytes/plant
  static const int EEPROM_BASE = 0;

  int plantAddr(uint8_t i) const {
    return EEPROM_BASE + (int)i * 6;
  }

  void loadPlantsFromEEPROM() {
    for (uint8_t i = 0; i < NUM_PLANTS; i++) {
      int addr = plantAddr(i);
      EEPROM.get(addr, plants[i].periodDays); addr += sizeof(uint16_t);
      EEPROM.get(addr, plants[i].mlPerWater); addr += sizeof(uint16_t);
      EEPROM.get(addr, plants[i].daysLeft);

      // sanity clamps
      if (plants[i].periodDays > DAYS_MAX) plants[i].periodDays = 0;
      if (plants[i].mlPerWater > ML_MAX)   plants[i].mlPerWater = 0;
      if (plants[i].daysLeft > DAYS_MAX)   plants[i].daysLeft = 0;

      // If period is 0, force countdown to 0
      if (plants[i].periodDays == 0) plants[i].daysLeft = 0;
    }
  }

  void savePlantToEEPROM(uint8_t i) {
    int addr = plantAddr(i);
    EEPROM.put(addr, plants[i].periodDays); addr += sizeof(uint16_t);
    EEPROM.put(addr, plants[i].mlPerWater); addr += sizeof(uint16_t);
    EEPROM.put(addr, plants[i].daysLeft);
  }

  void loadDraftFromPlant() {
    draftDays = plants[selectedPlant].periodDays;
    draftMl   = plants[selectedPlant].mlPerWater;
    editingLoaded = true;
  }

  // daily clock
  GlobalDayTimer dayTimer;

  // timer-screen scrolling
  uint8_t scrollIdx = 0;
  unsigned long lastScrollMs = 0;
  static const unsigned long scrollDelayMs = 250;

  // ---------- Rendering helpers ----------
  void print2(uint16_t v) { // 00..99
    if (v < 10) lcd.print('0');
    lcd.print(v);
  }

  void print3(uint16_t v) { // 000..999
    if (v < 100) lcd.print('0');
    if (v < 10)  lcd.print('0');
    lcd.print(v);
  }

  // ---------- RENDERING ----------
  void renderHome() {
    lcd.noBlink();
    lcd.setCursor(0, 0);
    lcd.print("Choose Settings ");

    lcd.setCursor(0, 1);
    lcd.print("1 2 3 4 5   HELP");

    static const uint8_t markCol[6] = {0, 2, 4, 6, 8, 12};
    lcd.setCursor(markCol[cursor], 1);
    lcd.blink();
  }

  void renderEditPlantHome() {
    lcd.noBlink();

    lcd.setCursor(0, 0);
    lcd.print("Plant ");
    lcd.print(selectedPlant + 1);
    lcd.print(" Settings   ");

    lcd.setCursor(0, 1);
    lcd.print("Edit Timer Back ");

    static const uint8_t markCol[3] = {0, 5, 11};
    lcd.setCursor(markCol[cursor], 1);
    lcd.blink();
  }

  void renderEditPlantValues() {
    lcd.noBlink();

    // Row 0: Days
    lcd.setCursor(0, 0);
    lcd.print("Days:");
    print2(draftDays);
    lcd.print(" - + SAVE");   // aim for 16 chars total

    // Row 1: mL
    lcd.setCursor(0, 1);
    lcd.print("mL:");
    print3(draftMl);
    lcd.print("  - + BACK");

    // Cursor at '-', '+', or SAVE/BACK
    // Layout positions:
    // "Days:DD - + SAVE"
    //        ^8 ^10 ^12 (approx)
    static const uint8_t colPos[3] = {8, 10, 12};
    uint8_t row = (editRow == 0) ? 0 : 1;
    lcd.setCursor(colPos[cursor], row);
    lcd.blink();
  }

  void renderTimer() {
  lcd.noBlink();

  // Top row
  lcd.setCursor(0, 0);
  lcd.print("Next Water In  "); // pad to 16 chars

  // Bottom row
  lcd.setCursor(0, 1);
  lcd.print("In ");
  lcd.print(plants[selectedPlant].daysLeft);
  lcd.print(" day(s)       "); // pad to clear leftovers
}



  void renderHelp() {
    lcd.noBlink();
    lcd.setCursor(0, 0);
    lcd.print("Help            ");
    lcd.setCursor(0, 1);
    lcd.print("Btn=Back        ");
  }

  void render() {
    if (screen == HOME) renderHome();
    else if (screen == EDIT_PLANT_HOME) renderEditPlantHome();
    else if (screen == EDIT_PLANT_VALUES) renderEditPlantValues();
    else if (screen == TIMER) renderTimer();
    else renderHelp();
  }
};

UI ui(lcd, joy);

// ---------------- Arduino sketch ----------------
void setup() {
  Serial.begin(9600);
  ui.begin();
}

void loop() {
  ui.update();
}