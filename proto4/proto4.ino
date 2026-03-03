#include <Arduino.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>
#include "Joystick.h"
#include "Timer.h"


// Declare joystick, lcd, and timer // -----------------------------
Timer timer(60);                         // 1 minute timer
Joystick joy(A6, A7, 12, 200, 50);       // Xpin, YPin, Button, deadzone, debounce
LiquidCrystal lcd(10, 9, 5, 4, 3, 2);       // Rs, E, D4, D5, D6, D7

// Declare plant struct and data array
// -----------------------------------
struct Plant {
  uint8_t  valvePin;     // GPIO pin controlling this plant’s valve
  uint16_t periodDays;   // days between watering
  uint16_t mlPerWater;   // mL per watering
  uint16_t daysLeft;     // countdown aligned to timer
};

static const uint8_t NUM_PLANTS = 5;

Plant plants[NUM_PLANTS] = {
  { A1,  0, 0, 0 },   // Plant 1 → valve on A1
  { A2,  0, 0, 0 },   // Plant 2 → valve on A2
  { A3,  0, 0, 0 },   // Plant 3 → valve on A3
  { A4,  0, 0, 0 },   // Plant 4 → valve on A4
  { A5,  0, 0, 0 }    // Plant 5 → valve on A5
};

// EEPROM helper functions // --------------------------------------
static const int EEPROM_BASE = 0;

static int plantAddr(uint8_t i) {
  return EEPROM_BASE + (int)i * 6;
}

static void loadPlantsFromEEPROM(Plant* p, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    int addr = plantAddr(i);
    EEPROM.get(addr, p[i].periodDays); addr += sizeof(uint16_t);
    EEPROM.get(addr, p[i].mlPerWater); addr += sizeof(uint16_t);
    EEPROM.get(addr, p[i].daysLeft);

    // sanity clamps
    if (p[i].periodDays > 99) p[i].periodDays = 0;
    if (p[i].mlPerWater > 999)   p[i].mlPerWater = 0;
    if (p[i].daysLeft > 99)   p[i].daysLeft = 0;

    // If period is 0, force countdown to 0
    if (p[i].periodDays == 0) p[i].daysLeft = 0;
  }
}

static void savePlantToEEPROM(const Plant* p, uint8_t i) {
  int addr = plantAddr(i);
  EEPROM.put(addr, p[i].periodDays); addr += sizeof(uint16_t);
  EEPROM.put(addr, p[i].mlPerWater); addr += sizeof(uint16_t);
  EEPROM.put(addr, p[i].daysLeft);
}

void clearEEPROM() {
  for (int i = 0; i < EEPROM.length(); i++) {
    EEPROM.write(i, 0xFF);   // common “erased” value
  }
}

// Do UI class // --------------------------------------------------
class UI {
public:
  UI(LiquidCrystal &lcd, Joystick &joy, Plant* plants, uint8_t nPlants)
    : lcd(lcd), joy(joy), plants(plants), nPlants(NUM_PLANTS) {}

  void begin() {
    lcd.begin(16, 2);
    lcd.clear();
    render();
  }

  void update() {
    unsigned long now = millis();

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
          screen = EDIT_PLANT_VALUES;
          cursor = 0;              // 0='-', 1='+', 2=SAVE/BACK
          editRow = 0;             // 0=Days, 1=mL
          editingLoaded = false;
          lcd.clear();
          dirty = true;
        }
        else if (cursor == 1) {
          screen = TIMER;
          lcd.clear();
          dirty = true;
        }
        else {
          screen = HOME;
          cursor = selectedPlant;
          lcd.clear();
          dirty = true;
        }
      }
    }

    // -------- EDIT_PLANT_VALUES --------
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
          if (editRow == 0) { if (draftDays > 0) draftDays--; }
          else { if (draftMl > 0) draftMl-= 10; }
          dirty = true;
        }
        else if (cursor == 1) {
          // '+'
          if (editRow == 0) { if (draftDays < 99) draftDays++; }
          else { if (draftMl < 999) draftMl+= 10; }
          dirty = true;
        }
        else { // cursor == 2
          if (editRow == 0) {
            // SAVE: commit + EEPROM
            plants[selectedPlant].periodDays = draftDays;
            plants[selectedPlant].mlPerWater = draftMl;

            // Reset countdown to new schedule
            plants[selectedPlant].daysLeft = plants[selectedPlant].periodDays;
            if (plants[selectedPlant].periodDays == 0) plants[selectedPlant].daysLeft = 0;

            savePlantToEEPROM(plants, selectedPlant);
          }
          // BACK always returns (discard if not saved)
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
      if (joy.justPressed()) {
        screen = EDIT_PLANT_HOME;
        cursor = 1;
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
  LiquidCrystal &lcd;
  Joystick &joy;
  Plant* plants;
  uint8_t nPlants;

  enum Screen : uint8_t {
    HOME,
    EDIT_PLANT_HOME,
    EDIT_PLANT_VALUES,
    TIMER,
    HELP
  };

  Screen screen = HOME;

  uint8_t cursor = 0;         // HOME:0..5, PlantHome:0..2, Values:0..2
  uint8_t selectedPlant = 0;  // 0..4
  uint8_t editRow = 0;        // 0=Days, 1=mL

  bool dirty = true;
  unsigned long lastRenderMs = 0;

  unsigned long lastMoveMs = 0;
  const unsigned long moveDelayMs = 180;

  // Draft values
  uint16_t draftDays = 0;
  uint16_t draftMl   = 0;
  bool editingLoaded = false;

  void loadDraftFromPlant() {
    draftDays = plants[selectedPlant].periodDays;
    draftMl   = plants[selectedPlant].mlPerWater;
    editingLoaded = true;
  }

  // Rendering helpers
  void print2(uint16_t v) {
    if (v < 10) lcd.print('0');
    lcd.print(v);
  }

  void print3(uint16_t v) {
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

    lcd.setCursor(0, 0);
    lcd.print("Days:");
    print2(draftDays);
    lcd.print(" - + SAVE");

    lcd.setCursor(0, 1);
    lcd.print("mL:");
    print3(draftMl);
    lcd.print("  - + BACK");

    static const uint8_t colPos[3] = {8, 10, 12};
    uint8_t row = (editRow == 0) ? 0 : 1;
    lcd.setCursor(colPos[cursor], row);
    lcd.blink();
  }

  void renderTimer() {
    lcd.noBlink();
    lcd.setCursor(0, 0);
    lcd.print("Next Water In  ");

    lcd.setCursor(0, 1);
    lcd.print("In ");
    lcd.print(plants[selectedPlant].daysLeft);
    lcd.print(" day(s)       ");
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

// Do watering class
class Watering
{
  public :
    Watering(uint8_t pump, LiquidCrystal& lcd, Plant* plants, uint8_t nPlants)
    : pump_out(pump), lcd(lcd), plants(plants), nPlants(nPlants) {}

      void water()
      {
        for (int i = 0; i < nPlants; ++i)
        {
          if (plants[i].periodDays != 0 && plants[i].mlPerWater != 0) // if it is a valid plant entry, decrement its day counter
          {
            plants[i].daysLeft -= 1;
            if (plants[i].daysLeft == 0)  // perform the water
            {
              digitalWrite(plants[i].valvePin, HIGH);
              digitalWrite(pump_out, HIGH);
              delay(plants[i].mlPerWater * 34);
              digitalWrite(plants[i].valvePin, LOW);
              digitalWrite(pump_out, LOW);
              plants[i].daysLeft = plants[i].periodDays;
            }
          }
        }
      }

      void water_setup()
      {
        pinMode(pump_out, OUTPUT);
        digitalWrite(pump_out, LOW);

        for (uint8_t i = 0; i < nPlants; i++) {
          pinMode(plants[i].valvePin, OUTPUT);
          digitalWrite(plants[i].valvePin, LOW);
        }
      }

  private:
    uint8_t pump_out;

    LiquidCrystal& lcd;
    Plant* plants;
    uint8_t nPlants;
};

// Declare UI and watering controller
UI ui(lcd, joy, plants, NUM_PLANTS);
Watering wat(A0, lcd, plants, NUM_PLANTS);

void setup() {
  // Start serial monitor
  Serial.begin(9600);
  // Load data from EEPROM
  loadPlantsFromEEPROM(plants, NUM_PLANTS);
  // Load home screen
  ui.begin();
  // Setup watering controller
  wat.water_setup();
  // OPTIONAL clear the EEPROM to reset
  clearEEPROM();
}

void loop() {
  // put your main code here, to run repeatedly:
  // if timer is at specified time, call watering function
  if (timer.isDone())
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Watering...");
      wat.water();
      ui.begin();
    }
  else
  {
    ui.update();
  }
}
