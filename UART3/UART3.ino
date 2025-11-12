#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal.h>

// Compass address
const uint8_t CMPS14_ADDRESS = 0x60;

// Compass Register addresses
const uint8_t CMPS14_BEARING_16BIT_HIGH = 0x02;
const uint8_t CMPS14_BEARING_16BIT_LOW = 0x03;

// Motor control pins
const uint8_t MOTOR_FORWARD = 1;
const uint8_t MOTOR_BACKWARD = 0;
const uint8_t MOTOR_L_DIR_PIN = 7;
const uint8_t MOTOR_R_DIR_PIN = 8;
const uint8_t MOTOR_L_PWM_PIN = 9;
const uint8_t MOTOR_R_PWM_PIN = 10;

// LCD pins
const int LCD_RS = 53;
const int LCD_E = 51;
const int LCD_D4 = 35;
const int LCD_D5 = 34;
const int LCD_D6 = 33;
const int LCD_D7 = 32;

// Target heading
float TARGET_HEADING = 0.0f;

// Heading tolerance
const float HEADING_OFFSET = 2.0f;

// Turn speed
const uint8_t TURN_SPEED_PERCENT = 25;

// Update interval
const unsigned long UPDATE_INTERVAL_MS = 200;

LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// North variable
float zeroHeading = 0.0f;

// functions
float readCompassDegrees();
uint16_t readCompass16Bit();
void find_heading(float given_degree);
void turnLeft(uint8_t speed);
void turnRight(uint8_t speed);
void stopMotors();
uint8_t percentToPwm(uint8_t percent);
void updateLCD(float target, float current);
float calculateTurnAngle(float current, float target);
float readCompassRaw();
void readAngleFromSerial();

unsigned long lastUpdate = 0;

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(100);
  Wire.begin();

  pinMode(MOTOR_L_DIR_PIN, OUTPUT);
  pinMode(MOTOR_R_DIR_PIN, OUTPUT);
  pinMode(MOTOR_L_PWM_PIN, OUTPUT);
  pinMode(MOTOR_R_PWM_PIN, OUTPUT);

  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Compass Turning");

  // North
  delay(500);
  zeroHeading = readCompassRaw();

  Serial.print("");
  stopMotors();
  delay(1000);
}

//MAIN LOOP
void loop() {
  readAngleFromSerial();

  unsigned long now = millis();
  if (now - lastUpdate >= UPDATE_INTERVAL_MS) {
    lastUpdate = now;
    find_heading(TARGET_HEADING);
  }
}

void find_heading(float given_degree) {
  float currentHeading = readCompassDegrees();
  float turnAngle = calculateTurnAngle(currentHeading, given_degree);

  updateLCD(given_degree, currentHeading);

  static bool targetReached = false;

  if (abs(turnAngle) <= HEADING_OFFSET) {
    stopMotors();
    if (!targetReached) {
      Serial.println("Waiting for new target! deg:<number>");
      targetReached = true;
    }
  } else {
    targetReached = false;
    if (turnAngle > 0) turnRight(TURN_SPEED_PERCENT);
    else turnLeft(TURN_SPEED_PERCENT);

    Serial.print("Current: "); Serial.print(currentHeading,1);
    Serial.print("° Target: "); Serial.print(given_degree,1);
    Serial.print("° Error: "); Serial.println(turnAngle,1);
  }
}

float readCompassDegrees() {
  float raw = readCompassRaw();
  float degrees = raw - zeroHeading;

  if (degrees < 0) degrees += 360;
  if (degrees >= 360) degrees -= 360;

  return degrees;
}

float readCompassRaw() {
  uint16_t bearing16bit = readCompass16Bit();
  return bearing16bit / 10.0f; // 0-359.9°
}

uint16_t readCompass16Bit() {
  Wire.beginTransmission(CMPS14_ADDRESS);
  Wire.write(CMPS14_BEARING_16BIT_HIGH);
  if (Wire.endTransmission() != 0) return 0;
  
  Wire.requestFrom(CMPS14_ADDRESS, (uint8_t)2);
  unsigned long startTime = millis();
  while (Wire.available() < 2) {
    if (millis() - startTime > 100) return 0;
  }

  uint8_t highByte = Wire.read();
  uint8_t lowByte = Wire.read();
  return (highByte << 8) | lowByte;
}

float calculateTurnAngle(float current, float target) {
  float diff = target - current;
  while (diff > 180) diff -= 360;
  while (diff < -180) diff += 360;
  return diff;
}

void turnLeft(uint8_t speed) {
  digitalWrite(MOTOR_L_DIR_PIN, MOTOR_BACKWARD);
  digitalWrite(MOTOR_R_DIR_PIN, MOTOR_FORWARD);
  uint8_t pwm = percentToPwm(speed);
  analogWrite(MOTOR_L_PWM_PIN, pwm);
  analogWrite(MOTOR_R_PWM_PIN, pwm);
}

void turnRight(uint8_t speed) {
  digitalWrite(MOTOR_L_DIR_PIN, MOTOR_FORWARD);
  digitalWrite(MOTOR_R_DIR_PIN, MOTOR_BACKWARD);
  uint8_t pwm = percentToPwm(speed);
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
  if (pwm >= 88 && pwm <= 92) pwm = 0; // deadzone
  return pwm;
}

void updateLCD(float target, float current) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Tgt:"); lcd.print(target,0); lcd.print((char)223);
  lcd.print(" Cur:"); lcd.print(current,0); lcd.print((char)223);

  lcd.setCursor(0, 1);
  float error = calculateTurnAngle(current, target);
  if (abs(error) <= HEADING_OFFSET) lcd.print("ON TARGET!");
  else {
    lcd.print("Err:"); lcd.print(error,1); lcd.print((char)223);
    lcd.print(error>0?" R":" L");
  }
}

void readAngleFromSerial() {
  while (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command.startsWith("deg:")) {
      String numberPart = command.substring(4);
      float angle = numberPart.toFloat();  

      //Correction
      angle = fmod(angle, 360.0f);
      if (angle < 0) angle += 360.0f;

      TARGET_HEADING = angle;
      Serial.print("New target heading set: ");
      Serial.println(TARGET_HEADING);

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Cmd:");
      lcd.print(command);
      lcd.setCursor(0, 1);
      lcd.print("Angle=");
      lcd.print(TARGET_HEADING);
      delay(200); // небольшая пауза для LCD
    } else {
      Serial.println("Invalid command! Use deg:<number>");
    }
  }
}
