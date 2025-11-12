#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal.h>

// ===== COMPASS SETTINGS =====
const uint8_t CMPS14_ADDRESS = 0x60;
const uint8_t CMPS14_BEARING_16BIT_HIGH = 0x02;
const uint8_t CMPS14_BEARING_16BIT_LOW = 0x03;

// ===== MOTOR CONTROL PINS =====
const uint8_t MOTOR_FORWARD = 1;   
const uint8_t MOTOR_BACKWARD = 0; 
const uint8_t MOTOR_L_DIR_PIN = 7;
const uint8_t MOTOR_R_DIR_PIN = 8;
const uint8_t MOTOR_L_PWM_PIN = 9;
const uint8_t MOTOR_R_PWM_PIN = 10;

// ===== ENCODER PINS =====
const uint8_t ENCODER_R_PIN = 2;
const uint8_t ENCODER_L_PIN = 3;
const uint8_t ENCODER_INTERRUPT_MODE = CHANGE;

// ===== LCD PINS =====
const int LCD_RS = 53;
const int LCD_E = 51;
const int LCD_D4 = 35;
const int LCD_D5 = 34;
const int LCD_D6 = 33;
const int LCD_D7 = 32;

// ===== DRIVE PARAMETERS =====
const uint8_t DRIVE_SPEED_PERCENT = 35;
const float PULSES_PER_CM_L = 84.0755f;

// ===== TURN PARAMETERS =====
const uint8_t TURN_SPEED_PERCENT = 25;
const float HEADING_OFFSET = 2.0f;

// ===== UPDATE INTERVALS =====
const unsigned long DRIVE_UPDATE_MS = 100;
const unsigned long TURN_UPDATE_MS = 200;

// ===== SYSTEM STATE =====
enum SystemState {
  IDLE,
  DRIVING,
  TURNING
};

// ===== GLOBAL VARIABLES =====
SystemState currentState = IDLE;
float targetDistance = 0.0f;
float targetHeading = 0.0f;
float zeroHeading = 0.0f;

// Encoder pulse counters
volatile unsigned long encoderRightPulses = 0;
volatile unsigned long encoderLeftPulses = 0;

// LCD object
LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// Update timing
unsigned long lastUpdateTime = 0;

// ===== FUNCTION DECLARATIONS =====
// Encoder ISR
void encoderRightISR();
void encoderLeftISR();

// Motor control
void startForwardDrive();
void startBackwardDrive();
void turnLeft(uint8_t speed);
void turnRight(uint8_t speed);
void stopMotors();
uint8_t percentToPwm(uint8_t percent);

// Compass functions
float readCompassRaw();
float readCompassDegrees();
uint16_t readCompass16Bit();
float calculateTurnAngle(float current, float target);

// Distance functions
float calculateDistance(unsigned long pulses);

// State processing
void processDriving();
void processTurning();

// Command parsing
void readCommandFromSerial();

// LCD update
void updateLCD();
void displayCustomMessage(String message);

// ===== SETUP =====
void setup() {
  // Motor pins
  pinMode(MOTOR_L_DIR_PIN, OUTPUT);
  pinMode(MOTOR_R_DIR_PIN, OUTPUT);
  pinMode(MOTOR_L_PWM_PIN, OUTPUT);
  pinMode(MOTOR_R_PWM_PIN, OUTPUT);

  // Encoder pins
  pinMode(ENCODER_R_PIN, INPUT);
  pinMode(ENCODER_L_PIN, INPUT);

  // Attach interrupts
  attachInterrupt(digitalPinToInterrupt(ENCODER_R_PIN), encoderRightISR, ENCODER_INTERRUPT_MODE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_L_PIN), encoderLeftISR, ENCODER_INTERRUPT_MODE);

  // I2C for compass
  Wire.begin();

  // LCD
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("UART4 System");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");

  // Serial
  Serial.begin(9600);
  Serial.setTimeout(100);

  // Calibrate compass zero heading
  delay(500);
  zeroHeading = readCompassRaw();

  // Ready
  stopMotors();
  currentState = IDLE;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MODE: IDLE");
  lcd.setCursor(0, 1);
  lcd.print("Ready...");

  Serial.println("=== UART4 System Ready ===");
  Serial.println("Commands:");
  Serial.println("  dist:<number>  - Drive distance in cm (+ forward, - backward)");
  Serial.println("  deg:<number>   - Turn to heading in degrees (0-359)");
}

// ===== MAIN LOOP =====
void loop() {
  // Always check for new commands
  readCommandFromSerial();

  // Process current state
  switch (currentState) {
    case IDLE:
      // Do nothing, wait for command
      break;

    case DRIVING:
      processDriving();
      break;

    case TURNING:
      processTurning();
      break;
  }
}

// ===== COMMAND PARSING =====
void readCommandFromSerial() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command.startsWith("dist:")) {
      String numberPart = command.substring(5);
      targetDistance = numberPart.toFloat();

      Serial.println("========================================");
      Serial.print(">> COMMAND: dist:");
      Serial.println(targetDistance);
      Serial.print(">> Target Distance: ");
      Serial.print(targetDistance);
      Serial.println(" cm");

      // Reset encoder counters
      encoderRightPulses = 0;
      encoderLeftPulses = 0;
      Serial.println(">> Encoder counters reset to 0");

      // Calculate target pulses for debugging
      unsigned long targetPulses = (unsigned long)(abs(targetDistance) * PULSES_PER_CM_L);
      Serial.print(">> Target pulses: ");
      Serial.println(targetPulses);

      // Start driving
      Serial.print(">> STATE CHANGE: ");
      if (currentState == IDLE) Serial.print("IDLE");
      else if (currentState == DRIVING) Serial.print("DRIVING");
      else if (currentState == TURNING) Serial.print("TURNING");
      Serial.println(" --> DRIVING");

      currentState = DRIVING;
      lastUpdateTime = millis();

      // Update LCD immediately
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MODE: DRIVING");
      lcd.setCursor(0, 1);
      lcd.print("Starting...");

      if (targetDistance >= 0) {
        Serial.println(">> Motor Direction: FORWARD");
        startForwardDrive();
      } else {
        Serial.println(">> Motor Direction: BACKWARD");
        startBackwardDrive();
      }

      Serial.println(">> Motor started!");
      Serial.println("========================================");
    }
    else if (command.startsWith("deg:")) {
      String numberPart = command.substring(4);
      float angle = numberPart.toFloat();

      Serial.println("========================================");
      Serial.print(">> COMMAND: deg:");
      Serial.println(angle);

      // Normalize angle to 0-360
      angle = fmod(angle, 360.0f);
      if (angle < 0) angle += 360.0f;

      targetHeading = angle;
      Serial.print(">> Target Heading: ");
      Serial.print(targetHeading);
      Serial.println(" degrees");

      // Start turning
      Serial.print(">> STATE CHANGE: ");
      if (currentState == IDLE) Serial.print("IDLE");
      else if (currentState == DRIVING) Serial.print("DRIVING");
      else if (currentState == TURNING) Serial.print("TURNING");
      Serial.println(" --> TURNING");

      currentState = TURNING;
      lastUpdateTime = millis();

      Serial.println(">> Turning mode activated!");
      Serial.println("========================================");
    }
    else if (command.startsWith("lcd:")) {
      String message = command.substring(4);

      Serial.println("========================================");
      Serial.print(">> COMMAND: lcd:");
      Serial.println(message);
      Serial.println(">> Displaying custom message on LCD");
      Serial.println("========================================");

      displayCustomMessage(message);
    }
    else {
      Serial.println("Invalid command! Use 'dist:<number>', 'deg:<number>', or 'lcd:<message>'");
    }
  }
}

// ===== STATE PROCESSING: DRIVING =====
void processDriving() {
  unsigned long targetPulses = (unsigned long)(abs(targetDistance) * PULSES_PER_CM_L);

  // Check if target reached
  if (encoderLeftPulses >= targetPulses) {
    stopMotors();

    float actualDistance = calculateDistance(encoderLeftPulses);

    Serial.println("========================================");
    Serial.println("=== DRIVING COMPLETED ===");
    Serial.print("Target Distance: ");
    Serial.print(targetDistance);
    Serial.println(" cm");
    Serial.print("Actual Distance: ");
    Serial.print(actualDistance);
    Serial.println(" cm");
    Serial.print("Final Encoder - R: ");
    Serial.print(encoderRightPulses);
    Serial.print(" L: ");
    Serial.println(encoderLeftPulses);
    Serial.println(">> STATE CHANGE: DRIVING --> IDLE");
    Serial.println("========================================");

    // Return to idle
    currentState = IDLE;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("FINISHED!");
    lcd.setCursor(0, 1);
    lcd.print("Dist:");
    lcd.print(actualDistance, 1);
    lcd.print("cm");

    delay(1500);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("MODE: IDLE");
    lcd.setCursor(0, 1);
    lcd.print("Ready...");
  }
  else {
    // Update LCD and debug info periodically
    if (millis() - lastUpdateTime >= DRIVE_UPDATE_MS) {
      updateLCD();

      // Debug output
      Serial.print("Driving... R:");
      Serial.print(encoderRightPulses);
      Serial.print(" L:");
      Serial.print(encoderLeftPulses);
      Serial.print(" Target:");
      Serial.print(targetPulses);
      Serial.print(" Dist:");
      Serial.print(calculateDistance(encoderLeftPulses), 1);
      Serial.println("cm");

      lastUpdateTime = millis();
    }
  }
}

// ===== STATE PROCESSING: TURNING =====
void processTurning() {
  if (millis() - lastUpdateTime >= TURN_UPDATE_MS) {
    float currentHeading = readCompassDegrees();
    float turnAngle = calculateTurnAngle(currentHeading, targetHeading);

    updateLCD();

    // Check if target reached
    if (abs(turnAngle) <= HEADING_OFFSET) {
      stopMotors();

      Serial.println("========================================");
      Serial.println("=== TURNING COMPLETED ===");
      Serial.print("Target Heading: ");
      Serial.print(targetHeading);
      Serial.println(" degrees");
      Serial.print("Final Heading: ");
      Serial.print(currentHeading);
      Serial.println(" degrees");
      Serial.print("Error: ");
      Serial.print(turnAngle);
      Serial.println(" degrees");
      Serial.println(">> STATE CHANGE: TURNING --> IDLE");
      Serial.println("========================================");

      // Return to idle
      currentState = IDLE;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("FINISHED!");
      lcd.setCursor(0, 1);
      lcd.print("Heading:");
      lcd.print(currentHeading, 0);
      lcd.print((char)223);

      delay(1500);

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MODE: IDLE");
      lcd.setCursor(0, 1);
      lcd.print("Ready...");
    }
    else {
      // Continue turning
      if (turnAngle > 0) {
        turnRight(TURN_SPEED_PERCENT);
      } else {
        turnLeft(TURN_SPEED_PERCENT);
      }

      Serial.print("Current: ");
      Serial.print(currentHeading, 1);
      Serial.print("° Target: ");
      Serial.print(targetHeading, 1);
      Serial.print("° Error: ");
      Serial.println(turnAngle, 1);
    }

    lastUpdateTime = millis();
  }
}

// ===== LCD UPDATE =====
void updateLCD() {
  lcd.clear();

  if (currentState == DRIVING) {
    float currentDistance = calculateDistance(encoderLeftPulses);
    lcd.setCursor(0, 0);
    lcd.print("R:");
    lcd.print(encoderRightPulses);
    lcd.print(" L:");
    lcd.print(encoderLeftPulses);
    lcd.setCursor(0, 1);
    lcd.print("Dist:");
    lcd.print(currentDistance, 1);
    lcd.print("cm");
  }
  else if (currentState == TURNING) {
    float currentHeading = readCompassDegrees();
    float error = calculateTurnAngle(currentHeading, targetHeading);

    lcd.setCursor(0, 0);
    lcd.print("MODE: TURNING");
    lcd.setCursor(0, 1);
    lcd.print("Ang:");
    lcd.print(currentHeading, 0);
    lcd.print((char)223);
    lcd.print(" E:");
    lcd.print(error, 0);
    lcd.print((char)223);
  }
}

void displayCustomMessage(String message) {
  lcd.clear();

  // If message is 16 characters or less, display on first line only
  if (message.length() <= 16) {
    lcd.setCursor(0, 0);
    lcd.print(message);
  }
  // If message is longer, split between two lines
  else {
    lcd.setCursor(0, 0);
    lcd.print(message.substring(0, 16));  // First 16 chars on line 1
    lcd.setCursor(0, 1);
    lcd.print(message.substring(16));     // Rest on line 2 (max 16 more)
  }
}

// ===== ENCODER ISR =====
void encoderRightISR() {
  encoderRightPulses++;
}

void encoderLeftISR() {
  encoderLeftPulses++;
}

// ===== MOTOR CONTROL =====
void startForwardDrive() {
  digitalWrite(MOTOR_L_DIR_PIN, MOTOR_FORWARD);
  digitalWrite(MOTOR_R_DIR_PIN, MOTOR_FORWARD);
  uint8_t pwm = percentToPwm(DRIVE_SPEED_PERCENT);

  Serial.print(">> PWM Value (Forward): ");
  Serial.print(pwm);
  Serial.print(" (");
  Serial.print(DRIVE_SPEED_PERCENT);
  Serial.println("%)");

  analogWrite(MOTOR_L_PWM_PIN, pwm);
  analogWrite(MOTOR_R_PWM_PIN, pwm);
}

void startBackwardDrive() {
  digitalWrite(MOTOR_L_DIR_PIN, MOTOR_BACKWARD);
  digitalWrite(MOTOR_R_DIR_PIN, MOTOR_BACKWARD);
  uint8_t pwm = percentToPwm(DRIVE_SPEED_PERCENT);

  Serial.print(">> PWM Value (Backward): ");
  Serial.print(pwm);
  Serial.print(" (");
  Serial.print(DRIVE_SPEED_PERCENT);
  Serial.println("%)");

  analogWrite(MOTOR_L_PWM_PIN, pwm);
  analogWrite(MOTOR_R_PWM_PIN, pwm);
}

void turnLeft(uint8_t speed) {
  digitalWrite(MOTOR_L_DIR_PIN, MOTOR_BACKWARD);
  digitalWrite(MOTOR_R_DIR_PIN, MOTOR_FORWARD);
  uint8_t pwm = percentToPwm(speed);

  // Apply deadzone only for turning
  if (pwm >= 85 && pwm <= 95) {
    pwm = 0;
  }

  analogWrite(MOTOR_L_PWM_PIN, pwm);
  analogWrite(MOTOR_R_PWM_PIN, pwm);
}

void turnRight(uint8_t speed) {
  digitalWrite(MOTOR_L_DIR_PIN, MOTOR_FORWARD);
  digitalWrite(MOTOR_R_DIR_PIN, MOTOR_BACKWARD);
  uint8_t pwm = percentToPwm(speed);

  // Apply deadzone only for turning
  if (pwm >= 85 && pwm <= 95) {
    pwm = 0;
  }

  analogWrite(MOTOR_L_PWM_PIN, pwm);
  analogWrite(MOTOR_R_PWM_PIN, pwm);
}

void stopMotors() {
  analogWrite(MOTOR_L_PWM_PIN, 0);
  analogWrite(MOTOR_R_PWM_PIN, 0);
}

uint8_t percentToPwm(uint8_t percent) {
  percent = constrain(percent, 0, 100);
  uint8_t pwm = (uint8_t)((percent * 255UL) / 100UL);
  return pwm;
}

// ===== COMPASS FUNCTIONS =====
float readCompassRaw() {
  uint16_t bearing16bit = readCompass16Bit();
  return bearing16bit / 10.0f; // Convert to degrees (0-359.9)
}

float readCompassDegrees() {
  float raw = readCompassRaw();
  float degrees = raw - zeroHeading;

  // Normalize to 0-360
  if (degrees < 0) degrees += 360;
  if (degrees >= 360) degrees -= 360;

  return degrees;
}

uint16_t readCompass16Bit() {
  Wire.beginTransmission(CMPS14_ADDRESS);
  Wire.write(CMPS14_BEARING_16BIT_HIGH);
  if (Wire.endTransmission() != 0) return 0;

  Wire.requestFrom(CMPS14_ADDRESS, (uint8_t)2);
  unsigned long startTime = millis();
  while (Wire.available() < 2) {
    if (millis() - startTime > 100) return 0; // Timeout
  }

  uint8_t highByte = Wire.read();
  uint8_t lowByte = Wire.read();
  return (highByte << 8) | lowByte;
}

float calculateTurnAngle(float current, float target) {
  float diff = target - current;

  // Find shortest path (-180 to +180)
  while (diff > 180) diff -= 360;
  while (diff < -180) diff += 360;

  return diff; // Positive = turn right, Negative = turn left
}

// ===== DISTANCE FUNCTIONS =====
float calculateDistance(unsigned long pulses) {
  if (PULSES_PER_CM_L > 0.0f) {
    return pulses / PULSES_PER_CM_L;
  }
  return 0.0f;
}
