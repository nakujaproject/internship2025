// Pin definitions
const int REMOTE_SWITCH = 27;  // Main arming switch
const int DROGUE_PYRO = 25;    // First pyro channel
const int MAIN_PYRO = 12;      // Second pyro channel

// Timer settings
unsigned long testDuration = 10 * 60 * 1000; // 10 minutes in milliseconds
unsigned long testStartTime = 0;
bool testRunning = false;
bool isArmed = false;

// For periodic pin state monitoring
unsigned long lastMonitorTime = 0;
const unsigned long monitorInterval = 1000; // 1 second

void printPinStates() {
  Serial.print("REMOTE_SWITCH: ");
  Serial.print(digitalRead(REMOTE_SWITCH));
  Serial.print(" | DROGUE_PYRO: ");
  Serial.print(digitalRead(DROGUE_PYRO));
  Serial.print(" | MAIN_PYRO: ");
  Serial.println(digitalRead(MAIN_PYRO));
}

void setup() {
  Serial.begin(115200);

  // Set pins as OUTPUT
  pinMode(REMOTE_SWITCH, OUTPUT);
  pinMode(DROGUE_PYRO, OUTPUT);
  pinMode(MAIN_PYRO, OUTPUT);

  // Ensure all are LOW at start
  digitalWrite(REMOTE_SWITCH, LOW);
  digitalWrite(DROGUE_PYRO, LOW);
  digitalWrite(MAIN_PYRO, LOW);

  Serial.println("=== Flight Computer Serial Control ===");
  Serial.println("Commands:");
  Serial.println("  r = arm system (REMOTE_SWITCH ON)");
  Serial.println("  a = activate DROGUE_PYRO (if armed)");
  Serial.println("  m = activate MAIN_PYRO (if armed)");
  Serial.println("  f = turn OFF REMOTE_SWITCH");
  Serial.println("  d = turn OFF DROGUE_PYRO");
  Serial.println("  n = turn OFF MAIN_PYRO");
  Serial.println();
}

void loop() {
  // Handle Serial input
  if (Serial.available()) {
    char command = Serial.read();

    if (command == 'r') {
      isArmed = true;
      digitalWrite(REMOTE_SWITCH, HIGH);
      testStartTime = millis();
      testRunning = true;
      Serial.println("System ARMED. 10-minute countdown started.");
    }

    if (isArmed && command == 'a') {
      digitalWrite(DROGUE_PYRO, HIGH);
      Serial.println("DROGUE_PYRO activated.");
    }

    if (isArmed && command == 'm') {
      digitalWrite(MAIN_PYRO, HIGH);
      Serial.println("MAIN_PYRO activated.");
    }

    // New: Turn OFF pins with commands
    if (command == 'f') {
      digitalWrite(REMOTE_SWITCH, LOW);
      Serial.println("REMOTE_SWITCH turned OFF.");
    }
    if (command == 'd') {
      digitalWrite(DROGUE_PYRO, LOW);
      Serial.println("DROGUE_PYRO turned OFF.");
    }
    if (command == 'n') {
      digitalWrite(MAIN_PYRO, LOW);
      Serial.println("MAIN_PYRO turned OFF.");
    }
  }

  // Periodically print pin states
  if (millis() - lastMonitorTime >= monitorInterval) {
    printPinStates();
    lastMonitorTime = millis();
  }

  // Check if 10 minutes have passed
  if (testRunning && millis() - testStartTime >= testDuration) {
    digitalWrite(REMOTE_SWITCH, LOW);
    digitalWrite(DROGUE_PYRO, LOW);
    digitalWrite(MAIN_PYRO, LOW);
    isArmed = false;
    testRunning = false;
    Serial.println("10 minutes elapsed. All switches turned OFF.");
  }
}