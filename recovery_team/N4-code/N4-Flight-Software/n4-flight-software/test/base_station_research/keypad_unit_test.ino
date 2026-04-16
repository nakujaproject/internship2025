#include <Wire.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>

// -------------------------
// LCD (20x4 I2C)
// Try 0x27 first. If blank, try 0x3F.
LiquidCrystal_I2C lcd(0x27, 20, 4);

// -------------------------
// 4x3 Keypad mapping
const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};

// Mega pin example (edit for your wiring)
byte rowPins[ROWS] = {52, 50, 48, 46};
byte colPins[COLS] = {53, 51, 49};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// -------------------------
// UI state
char lastKey = '-';
unsigned long keyCount = 0;
unsigned long lastPressMs = 0;
String history = "";

void refreshDisplay(bool keyEvent) {
  lcd.setCursor(0, 0);
  lcd.print("N4 Keypad Test 4x3  ");

  lcd.setCursor(0, 1);
  lcd.print("Last Key: ");
  lcd.print(lastKey);
  lcd.print("                ");

  lcd.setCursor(0, 2);
  lcd.print("Count:");
  lcd.print(keyCount);
  lcd.print("  T:");
  lcd.print(lastPressMs);
  lcd.print("ms      ");

  lcd.setCursor(0, 3);
  lcd.print("Seq:");
  lcd.print(history);
  lcd.print("                ");

  // Optional visual marker on key event
  if (keyEvent) {
    lcd.setCursor(19, 0);
    lcd.print("*");
  } else {
    lcd.setCursor(19, 0);
    lcd.print(" ");
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(); // Mega uses SDA=20, SCL=21

  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("N4 Base Station");
  lcd.setCursor(0, 1);
  lcd.print("Keypad + LCD Test");
  lcd.setCursor(0, 2);
  lcd.print("Press any key...");
  lcd.setCursor(0, 3);
  lcd.print("Ready");

  delay(1200);
  lcd.clear();
  refreshDisplay(false);
}

void loop() {
  char key = keypad.getKey();

  if (key) {
    lastKey = key;
    keyCount++;
    lastPressMs = millis();

    // Keep rolling sequence short enough for row width
    history += key;
    if (history.length() > 15) {
      history.remove(0, history.length() - 15);
    }

    Serial.print("Key pressed: ");
    Serial.println(key);

    refreshDisplay(true);
  } else {
    // Optional idle refresh every ~250 ms for smoother UI
    static unsigned long lastUi = 0;
    unsigned long now = millis();
    if (now - lastUi > 250) {
      lastUi = now;
      refreshDisplay(false);
    }
  }
}