#include <Arduino.h>
#include <LiquidCrystal.h>

// Motor control pins
const uint8_t MOTOR_FORWARD = 0;
const uint8_t MOTOR_BACKWARD = 1;
const uint8_t MOTOR_L_DIR_PIN = 7;
const uint8_t MOTOR_R_DIR_PIN = 8;
const uint8_t MOTOR_L_PWM_PIN = 9;
const uint8_t MOTOR_R_PWM_PIN = 10;

// Encoder pins
const uint8_t ENCODER_R_PIN = 2;  // Right encoder 
const uint8_t ENCODER_L_PIN = 3;  // Left encoder 
const uint8_t ENCODER_INTERRUPT_MODE = CHANGE;

// LCD pins (adjust according to your wiring)
const int LCD_RS = 53;
const int LCD_E = 51;
const int LCD_D4 = 35;
const int LCD_D5 = 34;
const int LCD_D6 = 33;
const int LCD_D7 = 32;

// Drive parameters
float TARGET_DISTANCE_CM = 15.0f;
const uint8_t DRIVE_SPEED_PERCENT = 35;

const float PULSES_PER_CM_L = 84.0755f;
const unsigned long UPDATE_INTERVAL_MS = 100;

// Encoder pulse counters
volatile unsigned long encoderRightPulses = 0;
volatile unsigned long encoderLeftPulses = 0;

// LCD object
LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// Function declarations
void encoderRightISR();
void encoderLeftISR();
void startForwardDrive();
void stopMotors();
uint8_t percentToPwm(uint8_t percent);
void updateLCD();
float calculateDistance(unsigned long pulses);
void readDistanceFromSerial();

void setup() {
  pinMode(MOTOR_L_DIR_PIN, OUTPUT);
  pinMode(MOTOR_R_DIR_PIN, OUTPUT);
  pinMode(MOTOR_L_PWM_PIN, OUTPUT);
  pinMode(MOTOR_R_PWM_PIN, OUTPUT);

  pinMode(ENCODER_R_PIN, INPUT);
  pinMode(ENCODER_L_PIN, INPUT);  

  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready...");

  Serial.begin(9600);
  Serial.println("Write dist:<number> to set distance.");

  attachInterrupt(digitalPinToInterrupt(ENCODER_R_PIN), encoderRightISR, ENCODER_INTERRUPT_MODE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_L_PIN), encoderLeftISR, ENCODER_INTERRUPT_MODE);
}

void loop() {
  readDistanceFromSerial();  // wait for command

  encoderRightPulses = 0;
  encoderLeftPulses = 0;

  unsigned long targetPulses = (unsigned long)(abs(TARGET_DISTANCE_CM) * PULSES_PER_CM_L);

  Serial.println("--Starting distance drive...--");
  Serial.print("Target distance: ");
  Serial.print(TARGET_DISTANCE_CM);
  Serial.println(" cm");

  if (TARGET_DISTANCE_CM >= 0)
  startForwardDrive();
  else 
  startBackwardDrive();

  unsigned long lastUpdateTime = millis();

  while (encoderLeftPulses < targetPulses) {
    if (millis() - lastUpdateTime >= UPDATE_INTERVAL_MS) {
      updateLCD();
      lastUpdateTime = millis();
    }
  }

  stopMotors();

  float actualDistance = calculateDistance(encoderLeftPulses);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FINISHED!");
  lcd.setCursor(0, 1);
  lcd.print("Dist:");
  lcd.print(actualDistance, 1);
  lcd.print("cm");

  Serial.println("=== DRIVE COMPLETED ===");
  Serial.print("Target distance: ");
  Serial.print(TARGET_DISTANCE_CM);
  Serial.println(" cm");
  Serial.print("Actual distance: ");
  Serial.print(actualDistance);
  Serial.println(" cm");

  delay(2000);

}

// Reading Distance
void readDistanceFromSerial() {
  String command = "";
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Waiting dist:");

  while (command.length() == 0) {
    if (Serial.available() > 0) {
      command = Serial.readStringUntil('\n');
      command.trim();
      
      if (command.startsWith("dist:")) {
        String numberPart = command.substring(5);
        int distance = numberPart.toInt();
        TARGET_DISTANCE_CM = distance;
        Serial.print("New distance set: ");
        Serial.println(TARGET_DISTANCE_CM);
          
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Cmd:");
        lcd.print(command);
        lcd.setCursor(0, 1);
        lcd.print("Dist=");
        lcd.print(TARGET_DISTANCE_CM);
        lcd.print("cm");
        delay(1000);
      }
      else {
      Serial.println("Invalid command! Use dist:<number>");
      command = "";
      }
    }
  }
}

void encoderRightISR() { encoderRightPulses++; }
void encoderLeftISR() { encoderLeftPulses++; }

void startForwardDrive() {
  digitalWrite(MOTOR_L_DIR_PIN, MOTOR_FORWARD);
  digitalWrite(MOTOR_R_DIR_PIN, MOTOR_FORWARD);
  uint8_t pwmValue = percentToPwm(DRIVE_SPEED_PERCENT);
  analogWrite(MOTOR_L_PWM_PIN, pwmValue);
  analogWrite(MOTOR_R_PWM_PIN, pwmValue);
}

void startBackwardDrive() {
  digitalWrite(MOTOR_L_DIR_PIN, MOTOR_BACKWARD);
  digitalWrite(MOTOR_R_DIR_PIN, MOTOR_BACKWARD);
  uint8_t pwmValue = percentToPwm(DRIVE_SPEED_PERCENT);
  analogWrite(MOTOR_L_PWM_PIN, pwmValue);
  analogWrite(MOTOR_R_PWM_PIN, pwmValue);
}

void stopMotors() {
  analogWrite(MOTOR_L_PWM_PIN, 0);
  analogWrite(MOTOR_R_PWM_PIN, 0);
}

uint8_t percentToPwm(uint8_t percent) {
  percent = constrain(percent, 0, 100);
  return (uint8_t)((percent * 255UL) / 100UL);
}

void updateLCD() {
  float currentDistance = calculateDistance(encoderLeftPulses);
  lcd.clear();
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

float calculateDistance(unsigned long pulses) {
  if (PULSES_PER_CM_L > 0.0f) {
    return pulses / PULSES_PER_CM_L;
  }
  return 0.0f;
}
