#include <Arduino.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>
#include "Joystick.h"

// ---------------- Hardware ----------------
Joystick joy(A6, A7, 8, 200, 50);   // deadzone, debounce threshold
LiquidCrystal lcd(7, 6, 5, 4, 3, 2);

// ---------------- Global daily timer ----------------
struct GlobalDayTimer {
  // static const uint32_t DAY_MS = 86400000UL; // 24h uptime-based
  static const uint32_t DAY_MS = 60000UL; // 1 minute = 1 "day"
  uint32_t lastTickMs = 0;

  void begin() { lastTickMs = millis(); }

  // true once per day (handles long pauses by skipping multiple days)
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

struct Plant {
  uint16_t periodDays = 0; // default 0
  uint16_t mlPerWater = 0; // default 0
  uint16_t daysLeft   = 0; // countdown aligned to timer
};

// ---------------- "Model" storage + EEPROM helpers ----------------
static const uint8_t NUM_PLANTS = 5;
Plant plants[NUM_PLANTS];

// Limits
static const uint16_t DAYS_MIN = 0;
static const uint16_t DAYS_MAX = 99;
static const uint16_t ML_MIN   = 0;
static const uint16_t ML_MAX   = 999;

// EEPROM layout: days (2) + ml (2) + daysLeft (2) = 6 bytes per plant
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
    if (p[i].periodDays > DAYS_MAX) p[i].periodDays = 0;
    if (p[i].mlPerWater > ML_MAX)   p[i].mlPerWater = 0;
    if (p[i].daysLeft > DAYS_MAX)   p[i].daysLeft = 0;

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

// ---------------- WaterController ----------------
// Owns global day tick + watering decision logic (outputs later)
// ---------------- WaterController ----------------
class WaterController {
public:
  WaterController(Plant* plants, uint8_t nPlants)
    : plants(plants), nPlants(nPlants) {}

  // Tune this: milliseconds of pump-on per mL delivered
  // Example placeholder: 20 ms/mL => 50 mL ~ 1s
  void setCalibrationMsPerMl(uint16_t msPerMl_) { msPerMl = msPerMl_; }

  void begin() {
    dayTimer.begin();

    pinMode(pumpPin, OUTPUT);
    digitalWrite(pumpPin, LOW);

    for (uint8_t i = 0; i < nPlants; i++) {
      pinMode(solenoidPins[i], OUTPUT);
      digitalWrite(solenoidPins[i], LOW);
    }
  }

  void update() {
    // 1) Daily tick scheduling
    if (dayTimer.update()) {
      onDayTick();
    }

    // 2) Watering state machine (non-blocking)
    serviceWatering();
  }

private:
  Plant* plants;
  uint8_t nPlants;

  GlobalDayTimer dayTimer;

  // Pin mapping: pump A0, solenoids A1..A6 (6 plants max)
  const uint8_t pumpPin = A0;
  uint8_t solenoidPins[6] = { A1, A2, A3, A4, A5, A6 };

  // Queue of plants to water (one-at-a-time)
  uint8_t queue[8];          // enough for small systems
  uint8_t qHead = 0, qTail = 0;
  bool wateringActive = false;
  uint8_t activePlant = 255;
  unsigned long waterStartMs = 0;
  unsigned long waterDurationMs = 0;

  uint16_t msPerMl = 20;     // <-- adjust to calibrate pump flow
  const uint16_t MIN_WATER_MS = 200;   // minimum on-time to overcome inertia
  const uint32_t MAX_WATER_MS = 30000; // safety cap: 30 seconds

  // ---------- Queue helpers ----------
  bool queueEmpty() const { return qHead == qTail; }

  bool queueFull() const {
    uint8_t nextTail = (uint8_t)((qTail + 1) % sizeof(queue));
    return nextTail == qHead;
  }

  void enqueue(uint8_t plantIdx) {
    if (queueFull()) return; // drop if full (or handle differently)
    queue[qTail] = plantIdx;
    qTail = (uint8_t)((qTail + 1) % sizeof(queue));
  }

  uint8_t dequeue() {
    if (queueEmpty()) return 255;
    uint8_t v = queue[qHead];
    qHead = (uint8_t)((qHead + 1) % sizeof(queue));
    return v;
  }

  // ---------- Day tick logic (your intended behavior) ----------
  void onDayTick() {
    for (uint8_t i = 0; i < nPlants; i++) {

      Serial.print("Day tick: plant ");
      Serial.print(i);
      Serial.print(" daysLeft=");
      Serial.println(plants[i].daysLeft);

      // Not configured
      if (plants[i].periodDays == 0) {
        plants[i].daysLeft = 0;
        continue;
      }

      // If today is watering day -> enqueue + reset
      if (plants[i].daysLeft == 0) {
        enqueue(i);
        plants[i].daysLeft = plants[i].periodDays;
      } else {
        // Otherwise decrement
        plants[i].daysLeft--;
      }
    }
  }

  // ---------- Watering (one solenoid at a time) ----------
  void serviceWatering() {
    if (!wateringActive) {
      if (queueEmpty()) return;

      activePlant = dequeue();
      if (activePlant == 255 || activePlant >= nPlants) return;

      // Compute duration proportional to mL
      // uint32_t dur = (uint32_t)plants[activePlant].mlPerWater * (uint32_t)msPerMl;
      const uint32_t TEST_WATER_MS = 5000; // 5 seconds
      uint32_t dur = TEST_WATER_MS;


      // Apply minimum and maximum bounds
      if (plants[activePlant].mlPerWater > 0 && dur < MIN_WATER_MS) dur = MIN_WATER_MS;
      if (dur > MAX_WATER_MS) dur = MAX_WATER_MS;

      // If mlPerWater is 0, treat as "do nothing" (skip watering)
      if (plants[activePlant].mlPerWater == 0) {
        activePlant = 255;
        return;
      }

      // Start watering: ensure all solenoids off, then enable the one we want
      allSolenoidsOff();
      digitalWrite(solenoidPins[activePlant], HIGH);
      digitalWrite(pumpPin, HIGH);

      waterStartMs = millis();
      waterDurationMs = dur;
      wateringActive = true;
      return;
    }

    // If active, check if time elapsed
    if ((unsigned long)(millis() - waterStartMs) >= waterDurationMs) {
      // Stop outputs
      digitalWrite(pumpPin, LOW);
      allSolenoidsOff();

      wateringActive = false;
      activePlant = 255;
    }
  }

  void allSolenoidsOff() {
    for (uint8_t i = 0; i < nPlants; i++) {
      digitalWrite(solenoidPins[i], LOW);
    }
  }
};


// ---------------- UI ----------------
class UI {
public:
  UI(LiquidCrystal &lcd, Joystick &joy, Plant* plants, int nPlants)
    : lcd(lcd), joy(joy), plants(plants), nPlants(nPlants) {}

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
    lcd.print(":) Plant ");
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

// ---------------- Instantiate controller + UI ----------------
WaterController controller(plants, NUM_PLANTS);
UI ui(lcd, joy, plants, NUM_PLANTS);

// ---------------- Arduino sketch ----------------
void setup() {
  Serial.begin(9600);

  loadPlantsFromEEPROM(plants, NUM_PLANTS);
  controller.setCalibrationMsPerMl(20); // <-- tune this
  controller.begin();
  ui.begin();
}

void loop() {
  controller.update(); // ✅ background day tick + watering checks
  ui.update();         // ✅ UI only
}
