// ================= BLYNK CONFIGURATION =================
#define BLYNK_TEMPLATE_ID "YOUR_BLYNK_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "Smart Lock"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"
#define BLYNK_PRINT Serial

// ================= LIBRARIES =================
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <SPI.h>
#include <Wire.h>
#include "time.h"
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= OLED SETUP =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

#include <FluxGarage_RoboEyes.h>
#include <Adafruit_Fingerprint.h>
#include <ESP32Servo.h>

// ================= WIFI & TIME CONFIGURATION =================

// Replace only for private/local testing.
// Do NOT publish real credentials on GitHub.
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;       // IST
const int daylightOffset_sec = 0;

// ================= PIN DEFINITIONS =================

#define RST_PIN 4
#define SS_PIN 5

#define FINGER_RX 16
#define FINGER_TX 17

#define LOCK_SERVO_PIN 25
#define DOOR_SERVO_PIN 26

// ================= RFID CONFIGURATION =================

// Do NOT publish your real RFID UID.
String AUTHORIZED_UID = "YOUR_AUTHORIZED_UID";

// ================= SERVO CONFIGURATION =================

const int LOCK_LOCKED_POS = 180;
const int LOCK_UNLOCKED_POS = 25;

const int DOOR_CLOSED_POS = 180;
const int DOOR_OPEN_POS = 25;

const int DOOR_OPEN_DURATION = 5000;    // 5 seconds

// ================= MODULE INSTANCES =================

MFRC522 mfrc522(SS_PIN, RST_PIN);

roboEyes roboEyes;

bool showEyes = false;

unsigned long eventTimer;

bool event1wasPlayed = 0;
bool event2wasPlayed = 0;
bool event3wasPlayed = 0;

HardwareSerial mySerial(2);

Adafruit_Fingerprint finger =
  Adafruit_Fingerprint(&mySerial);

Servo lockServo;
Servo doorServo;


// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

void unlockDoorSequence();
void displayMessage(String line1, String line2);
void resetIdleAnimation();
void updateIdleDisplay();
void handleRFID();
int checkFingerprint();


// ============================================================
// BLYNK VIRTUAL PINS
// ============================================================

// V1: Remote Unlock Trigger
BLYNK_WRITE(V1)
{
  int value = param.asInt();

  if (value == 1)
  {
    Serial.println("[BLYNK] Remote Unlock Triggered!");

    unlockDoorSequence();

    // Reset button in Blynk application
    Blynk.virtualWrite(V1, 0);
  }
}


// V2: New Card Enrollment Trigger
BLYNK_WRITE(V2)
{
  if (param.asInt() == 1)
  {
    Serial.println("[BLYNK] Enrollment Mode: New Card");

    showEyes = false;

    displayMessage(
      "Enrollment Mode",
      "Scan New Card..."
    );

    // Logic for saving the next scanned UID
    // would be implemented here.

    delay(5000);

    resetIdleAnimation();

    Blynk.virtualWrite(V2, 0);
  }
}


// V3: New Finger Enrollment Trigger
BLYNK_WRITE(V3)
{
  if (param.asInt() == 1)
  {
    Serial.println("[BLYNK] Enrollment Mode: New Finger");

    showEyes = false;

    displayMessage(
      "Enrollment Mode",
      "Place Finger..."
    );

    // Logic for fingerprint enrollment
    // would be implemented here.

    delay(5000);

    resetIdleAnimation();

    Blynk.virtualWrite(V3, 0);
  }
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  while (!Serial);

  Serial.println("\n[SYSTEM] Booting up...");


  // ---------------- OLED INITIALIZATION ----------------

  if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        0x3C))
  {
    Serial.println("[ERROR] OLED failed");

    for (;;);
  }


  // ---------------- ROBO EYES INITIALIZATION ----------------

  roboEyes.begin(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    100
  );

  roboEyes.setPosition(DEFAULT);
  roboEyes.close();


  // ---------------- CONNECTION MESSAGE ----------------

  displayMessage(
    "Connecting...",
    "WiFi & Blynk"
  );


  // ---------------- WIFI & BLYNK ----------------

  // Credentials are placeholders in this
  // public GitHub version.

  Blynk.begin(
    BLYNK_AUTH_TOKEN,
    ssid,
    pass
  );

  Serial.println(
    "\n[WIFI & BLYNK] Connected!"
  );


  // ---------------- TIME SETUP ----------------

  configTime(
    gmtOffset_sec,
    daylightOffset_sec,
    ntpServer
  );


  // ---------------- SERVO INITIALIZATION ----------------

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  lockServo.attach(
    LOCK_SERVO_PIN
  );

  doorServo.attach(
    DOOR_SERVO_PIN
  );

  lockServo.write(
    LOCK_LOCKED_POS
  );

  doorServo.write(
    DOOR_CLOSED_POS
  );

  delay(1000);

  lockServo.detach();
  doorServo.detach();


  // ---------------- RFID INITIALIZATION ----------------

  SPI.begin();

  mfrc522.PCD_Init();


  // ---------------- FINGERPRINT INITIALIZATION ----------------

  finger.begin(57600);

  if (finger.verifyPassword())
  {
    Serial.println(
      "[SYSTEM] Fingerprint sensor found."
    );
  }
  else
  {
    Serial.println(
      "[ERROR] Fingerprint sensor not found."
    );
  }


  // ---------------- IDLE DISPLAY ----------------

  showEyes = true;

  eventTimer = millis();
}


// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{
  // Keep Blynk connection active
  Blynk.run();


  // Update idle OLED animation
  if (showEyes)
  {
    updateIdleDisplay();
  }


  // ==========================================================
  // 1. CHECK RFID
  // ==========================================================

  if (
    mfrc522.PICC_IsNewCardPresent() &&
    mfrc522.PICC_ReadCardSerial()
  )
  {
    handleRFID();
  }


  // ==========================================================
  // 2. CHECK FINGERPRINT
  // ==========================================================

  int fingerID = checkFingerprint();


  if (fingerID > 0)
  {
    showEyes = false;

    displayMessage(
      "Biometric OK",
      "Unlocking..."
    );

    unlockDoorSequence();

    resetIdleAnimation();
  }


  else if (fingerID == -1)
  {
    showEyes = false;

    displayMessage(
      "Access Denied",
      "Unknown Finger"
    );

    delay(2000);

    resetIdleAnimation();
  }


  delay(1);
}


// ============================================================
// IDLE DISPLAY
// ============================================================

void updateIdleDisplay()
{
  roboEyes.update();


  // ---------------- DISPLAY DATE & TIME ----------------

  struct tm timeinfo;

  if (
    getLocalTime(
      &timeinfo,
      10
    )
  )
  {
    display.fillRect(
      0,
      54,
      128,
      10,
      SSD1306_BLACK
    );


    char timeStr[30];


    strftime(
      timeStr,
      sizeof(timeStr),
      "%d/%m/%Y %H:%M",
      &timeinfo
    );


    display.setTextSize(1);

    display.setTextColor(
      SSD1306_WHITE
    );


    int16_t x1, y1;

    uint16_t w, h;


    display.getTextBounds(
      timeStr,
      0,
      55,
      &x1,
      &y1,
      &w,
      &h
    );


    display.setCursor(
      (SCREEN_WIDTH - w) / 2,
      55
    );


    display.print(timeStr);

    display.display();
  }


  // ---------------- ROBO EYES ANIMATION ----------------

  if (
    millis() >= eventTimer + 2000 &&
    !event1wasPlayed
  )
  {
    event1wasPlayed = 1;

    roboEyes.open();
  }


  if (
    millis() >= eventTimer + 4000 &&
    !event2wasPlayed
  )
  {
    event2wasPlayed = 1;

    roboEyes.setMood(HAPPY);

    roboEyes.anim_laugh();
  }


  if (
    millis() >= eventTimer + 6000 &&
    !event3wasPlayed
  )
  {
    event3wasPlayed = 1;

    roboEyes.setMood(TIRED);
  }


  if (millis() >= eventTimer + 8000)
  {
    roboEyes.close();

    roboEyes.setMood(DEFAULT);

    eventTimer = millis();

    event1wasPlayed = 0;
    event2wasPlayed = 0;
    event3wasPlayed = 0;
  }
}


// ============================================================
// RFID HANDLER
// ============================================================

void handleRFID()
{
  showEyes = false;

  String readUID = "";


  // Read UID from RFID card
  for (
    byte i = 0;
    i < mfrc522.uid.size;
    i++
  )
  {
    readUID += String(
      mfrc522.uid.uidByte[i] < 0x10
        ? " 0"
        : " "
    );

    readUID += String(
      mfrc522.uid.uidByte[i],
      HEX
    );
  }


  readUID.trim();

  readUID.toUpperCase();


  // ---------------- AUTHENTICATION ----------------

  if (readUID == AUTHORIZED_UID)
  {
    displayMessage(
      "RFID Accepted",
      "Unlocking..."
    );

    unlockDoorSequence();
  }


  else
  {
    displayMessage(
      "Access Denied",
      "Invalid Card"
    );

    delay(2000);
  }


  // Stop communication with card
  mfrc522.PICC_HaltA();


  resetIdleAnimation();
}


// ============================================================
// RESET IDLE ANIMATION
// ============================================================

void resetIdleAnimation()
{
  roboEyes.close();

  roboEyes.setMood(DEFAULT);

  eventTimer = millis();

  event1wasPlayed =
    event2wasPlayed =
    event3wasPlayed = 0;

  showEyes = true;
}


// ============================================================
// OLED MESSAGE FUNCTION
// ============================================================

void displayMessage(
  String line1,
  String line2
)
{
  Serial.println(
    "[OLED] " +
    line1 +
    " | " +
    line2
  );


  display.clearDisplay();


  display.setTextSize(1);

  display.setTextColor(
    SSD1306_WHITE
  );


  display.setCursor(
    0,
    10
  );

  display.println(line1);


  display.setCursor(
    0,
    30
  );

  display.println(line2);


  display.display();
}


// ============================================================
// DOOR UNLOCK SEQUENCE
// ============================================================

void unlockDoorSequence()
{
  showEyes = false;


  // ==========================================================
  // 1. UNLOCK
  // ==========================================================

  Serial.println(
    "[SERVO] Moving to Unlocked position..."
  );


  lockServo.attach(
    LOCK_SERVO_PIN
  );


  lockServo.write(
    LOCK_UNLOCKED_POS
  );


  delay(1000);


  lockServo.detach();


  // ==========================================================
  // 2. OPEN DOOR
  // ==========================================================

  Serial.println(
    "[SERVO] Moving to Open position..."
  );


  doorServo.attach(
    DOOR_SERVO_PIN
  );


  doorServo.write(
    DOOR_OPEN_POS
  );


  displayMessage(
    "Door is Open",
    "Access Granted"
  );


  delay(1000);


  doorServo.detach();


  // ==========================================================
  // 3. WAIT WHILE DOOR IS OPEN
  // ==========================================================

  Serial.println(
    "[SYSTEM] Door open. Waiting 5 seconds..."
  );


  unsigned long startWait = millis();


  while (
    millis() - startWait <
    DOOR_OPEN_DURATION
  )
  {
    Blynk.run();

    yield();
  }


  Serial.println(
    "[SYSTEM] Wait over. Returning to secure state."
  );


  displayMessage(
    "Closing...",
    "Stand clear"
  );


  // ==========================================================
  // 4. CLOSE DOOR
  // ==========================================================

  Serial.println(
    "[SERVO] Returning Door to Closed position..."
  );


  doorServo.attach(
    DOOR_SERVO_PIN
  );


  doorServo.write(
    DOOR_CLOSED_POS
  );


  delay(1500);


  doorServo.detach();


  // ==========================================================
  // 5. LOCK DOOR
  // ==========================================================

  Serial.println(
    "[SERVO] Returning Lock to Secured position..."
  );


  lockServo.attach(
    LOCK_SERVO_PIN
  );


  lockServo.write(
    LOCK_LOCKED_POS
  );


  delay(1000);


  lockServo.detach();


  Serial.println(
    "[SYSTEM] Door secured."
  );
}


// ============================================================
// FINGERPRINT CHECK
// ============================================================

int checkFingerprint()
{
  uint8_t p;


  // Capture fingerprint image
  p = finger.getImage();


  if (p != FINGERPRINT_OK)
  {
    return 0;
  }


  // Convert image to template
  p = finger.image2Tz();


  if (p != FINGERPRINT_OK)
  {
    return 0;
  }


  // Search stored fingerprints
  p = finger.fingerSearch();


  if (p == FINGERPRINT_OK)
  {
    return finger.fingerID;
  }


  else if (p == FINGERPRINT_NOTFOUND)
  {
    return -1;
  }


  return 0;
}
