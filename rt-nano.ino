/*
 * Sailing Regatta Timer
 * Based on regatta-timer-ai-spec.md
 *
 * Target: Arduino UNO
 * Pins:
 *  - Display CLK: 2
 *  - Display DIO: 3
 *  - Buzzer: 8
 *  - Buttons: 4 (1min), 5 (2min), 6 (3min), 7 (5min)
 *
 * Logging: Serial 9600, JUnit XML format
 */

#include <Arduino.h>
#include <TM1637Display.h>
#include <avr/pgmspace.h>

// --- Configuration ---
#define BAUD_RATE 115200
#define MILLIS_PER_SECOND 1000 // Real-world time
#define PIN_CLK 2
#define PIN_DIO 3
#define PIN_BUZZER 8
#define PIN_BTN_1MIN 4
#define PIN_BTN_2MIN 5
#define PIN_BTN_3MIN 6
#define PIN_BTN_5MIN 7

#define DISPLAY_BRIGHTNESS 0x0f

#define BUZZ_LONG_MS 400
#define BUZZ_SHORT_MS 150
#define BUZZ_GAP_MS 150
#define TIMER_INTERVAL_MS 1000

// --- Data Structures ---
struct BuzzEvent {
  int seconds;        // Elapsed time (s) when to use this event
  uint8_t longCount;  // Number of long buzzes
  uint8_t shortCount; // Number of short buzzes
};

// --- Sequences (PROGMEM) ---

// 1-Minute Sequence
const BuzzEvent seq1Min[] PROGMEM = {
    {0, 1, 0},  {30, 0, 3}, {40, 0, 2}, {50, 0, 1}, {55, 0, 1},
    {56, 0, 1}, {57, 0, 1}, {58, 0, 1}, {59, 0, 1}, {60, 1, 0}};

// 2-Minute Sequence
const BuzzEvent seq2Min[] PROGMEM = {{0, 2, 0},   {30, 1, 3},  {60, 1, 0},
                                     {90, 0, 3},  {100, 0, 2}, {110, 0, 1},
                                     {115, 0, 1}, {116, 0, 1}, {117, 0, 1},
                                     {118, 0, 1}, {119, 0, 1}, {120, 1, 0}};

// 3-Minute Sequence
const BuzzEvent seq3Min[] PROGMEM = {
    {0, 3, 0},   {60, 2, 0},  {90, 1, 3},  {120, 1, 0}, {150, 0, 3},
    {160, 0, 2}, {170, 0, 1}, {175, 0, 1}, {176, 0, 1}, {177, 0, 1},
    {178, 0, 1}, {179, 0, 1}, {180, 1, 0}};

// 5-Minute Sequence
const BuzzEvent seq5Min[] PROGMEM = {
    {0, 1, 0}, {60, 1, 0}, {240, 1, 0}, {300, 1, 0}};

// --- Globals ---
TM1637Display display(PIN_CLK, PIN_DIO);

// --- Buzzer State Machine ---
enum BuzzerState { BZ_IDLE, BZ_BUZZING, BZ_GAPPING };
BuzzerState bzState = BZ_IDLE;
uint8_t bzLongsLeft = 0;
uint8_t bzShortsLeft = 0;
unsigned long bzNextToggle = 0;

void updateBuzzer() {
  if (bzState == BZ_IDLE)
    return;

  if (millis() < bzNextToggle)
    return;

  if (bzState == BZ_BUZZING) {
    digitalWrite(PIN_BUZZER, LOW);
    if (bzLongsLeft > 0)
      bzLongsLeft--;
    else if (bzShortsLeft > 0)
      bzShortsLeft--;

    if (bzLongsLeft > 0 || bzShortsLeft > 0) {
      bzState = BZ_GAPPING;
      bzNextToggle = millis() + BUZZ_GAP_MS;
    } else {
      bzState = BZ_IDLE;
    }
  } else if (bzState == BZ_GAPPING) {
    bzState = BZ_BUZZING;
    digitalWrite(PIN_BUZZER, HIGH);
    bzNextToggle = millis() + (bzLongsLeft > 0 ? BUZZ_LONG_MS : BUZZ_SHORT_MS);
  }
}

void triggerBuzzes(int longCount, int shortCount, int elapsed,
                   const char *testName) {
  // Log BuzzerEvent
  Serial.print(F("<testcase classname=\"BuzzerEvent\" whichtest=\""));
  Serial.print(testName);
  Serial.print(F("\" elapsed=\""));
  Serial.print(elapsed);
  Serial.print(F("\" type=\"Buzzer\" longcount=\""));
  Serial.print(longCount);
  Serial.print(F("\" shortcount=\""));
  Serial.print(shortCount);
  Serial.println(F("\"/>"));

  if (longCount == 0 && shortCount == 0)
    return;

  // Initialize state machine
  bzLongsLeft = longCount;
  bzShortsLeft = shortCount;
  bzState = BZ_BUZZING;
  digitalWrite(PIN_BUZZER, HIGH);
  bzNextToggle = millis() + (bzLongsLeft > 0 ? BUZZ_LONG_MS : BUZZ_SHORT_MS);
}

// --- Functions ---

// playBuzzes replaced by non-blocking triggerBuzzes and updateBuzzer

void updateDisplay(int secondsRemaining) {
  // MM:SS format
  int m = secondsRemaining / 60;
  int s = secondsRemaining % 60;
  // Format: minutes * 100 + seconds
  // Colon bit mask: 0b01000000 (which is 0x40)
  // showNumberDecEx(number, dots, leading_zeros)
  // dots = 0x40 for colon (default in library usually, but spec says
  // 0b01000000)
  display.showNumberDecEx(m * 100 + s, 0b01000000, true);
}

void runSequence(const BuzzEvent *events, int eventCount, int duration,
                 const char *name) {
  // Log StartEvent
  Serial.print(F("<testcase classname=\"StartEvent\" whichtest=\""));
  Serial.print(name);
  Serial.print(F("\" elapsed=\"0\" type=\"Start\"/>\n"));

  unsigned long startMillis = millis();
  int lastElapsed = -1;

  while (true) {
    unsigned long currentMillis = millis();
    updateBuzzer();

    // Calculate elapsed time based on startMillis to avoid cumulative drift
    int elapsed = (int)((currentMillis - startMillis) / MILLIS_PER_SECOND);

    if (elapsed > lastElapsed) {
      if (elapsed > duration)
        break;

      // New second threshold reached
      int remaining = duration - elapsed;
      updateDisplay(remaining);

      // Check for events
      int lCount = 0;
      int sCount = 0;
      bool doBuzz = false;
      for (int i = 0; i < eventCount; i++) {
        int evSeconds = (int)pgm_read_word(&events[i].seconds);
        if (evSeconds == elapsed) {
          BuzzEvent ev;
          memcpy_P(&ev, &events[i], sizeof(BuzzEvent));
          lCount = ev.longCount;
          sCount = ev.shortCount;
          doBuzz = true;
          break;
        }
      }

      if (elapsed == duration) {
        // Log EndEvent before final buzzer
        Serial.print(F("<testcase classname=\"EndEvent\" whichtest=\""));
        Serial.print(name);
        Serial.print(F("\" elapsed=\""));
        Serial.print(elapsed);
        Serial.println(F("\" type=\"End\"/>"));
      }

      if (doBuzz) {
        triggerBuzzes(lCount, sCount, elapsed, name);
      }

      lastElapsed = elapsed;
    }

    // Yield to avoid blocking other potential background tasks if added later
    // In this simple sketch, we just loop as fast as possible for timing
    // precision.
  }

  // Final wait for buzzers to finish if needed?
  // Spec implies we exit sequence immediately after duration, but buzzer
  // might still be going.
  while (bzState != BZ_IDLE) {
    updateBuzzer();
  }
}

// Track previous button states for edge detection, initialized to HIGH by
// default
int lastBtn1 = HIGH;
int lastBtn2 = HIGH;
int lastBtn3 = HIGH;
int lastBtn5 = HIGH;

void setup() {
  Serial.begin(BAUD_RATE);

  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_DIO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  pinMode(PIN_BTN_1MIN, INPUT_PULLUP);
  pinMode(PIN_BTN_2MIN, INPUT_PULLUP);
  pinMode(PIN_BTN_3MIN, INPUT_PULLUP);
  pinMode(PIN_BTN_5MIN, INPUT_PULLUP);

  display.setBrightness(DISPLAY_BRIGHTNESS);
  display.showNumberDecEx(0, 0b01000000, true); // initial 00:00

  // Read initial state so we only trigger on CHANGES (switches from HIGH to
  // LOW) This fixes the auto-start issue if a button is stuck LOW at boot.
  lastBtn1 = digitalRead(PIN_BTN_1MIN);
  lastBtn2 = digitalRead(PIN_BTN_2MIN);
  lastBtn3 = digitalRead(PIN_BTN_3MIN);
  lastBtn5 = digitalRead(PIN_BTN_5MIN);

  Serial.println(F("--- SUT Boot ---"));
  Serial.print(F("1m="));
  Serial.print(digitalRead(PIN_BTN_1MIN));
  Serial.print(F(" 2m="));
  Serial.print(digitalRead(PIN_BTN_2MIN));
  Serial.print(F(" 3m="));
  Serial.print(digitalRead(PIN_BTN_3MIN));
  Serial.print(F(" 5m="));
  Serial.println(digitalRead(PIN_BTN_5MIN));
  Serial.println(F("Ready for buttons..."));
}

void loop() {
  // Read current states
  int btn1 = digitalRead(PIN_BTN_1MIN);
  int btn2 = digitalRead(PIN_BTN_2MIN);
  int btn3 = digitalRead(PIN_BTN_3MIN);
  int btn5 = digitalRead(PIN_BTN_5MIN);

  // Check for Press (HIGH to LOW transition)
  // 1 Minute Button
  if (lastBtn1 == HIGH && btn1 == LOW) {
    delay(50);
    if (digitalRead(PIN_BTN_1MIN) == LOW) {
      runSequence(seq1Min, sizeof(seq1Min) / sizeof(BuzzEvent), 60, "1min");
      // Update states after blocking sequence
      lastBtn1 = digitalRead(PIN_BTN_1MIN);
      lastBtn2 = digitalRead(PIN_BTN_2MIN);
      lastBtn3 = digitalRead(PIN_BTN_3MIN);
      lastBtn5 = digitalRead(PIN_BTN_5MIN);
      return;
    }
  }

  // 2 Minute Button
  else if (lastBtn2 == HIGH && btn2 == LOW) {
    delay(50);
    if (digitalRead(PIN_BTN_2MIN) == LOW) {
      runSequence(seq2Min, sizeof(seq2Min) / sizeof(BuzzEvent), 120, "2min");
      lastBtn1 = digitalRead(PIN_BTN_1MIN);
      lastBtn2 = digitalRead(PIN_BTN_2MIN);
      lastBtn3 = digitalRead(PIN_BTN_3MIN);
      lastBtn5 = digitalRead(PIN_BTN_5MIN);
      return;
    }
  }

  // 3 Minute Button
  else if (lastBtn3 == HIGH && btn3 == LOW) {
    delay(50);
    if (digitalRead(PIN_BTN_3MIN) == LOW) {
      runSequence(seq3Min, sizeof(seq3Min) / sizeof(BuzzEvent), 180, "3min");
      lastBtn1 = digitalRead(PIN_BTN_1MIN);
      lastBtn2 = digitalRead(PIN_BTN_2MIN);
      lastBtn3 = digitalRead(PIN_BTN_3MIN);
      lastBtn5 = digitalRead(PIN_BTN_5MIN);
      return;
    }
  }

  // 5 Minute Button
  else if (lastBtn5 == HIGH && btn5 == LOW) {
    delay(50);
    if (digitalRead(PIN_BTN_5MIN) == LOW) {
      runSequence(seq5Min, sizeof(seq5Min) / sizeof(BuzzEvent), 300, "5min");
      lastBtn1 = digitalRead(PIN_BTN_1MIN);
      lastBtn2 = digitalRead(PIN_BTN_2MIN);
      lastBtn3 = digitalRead(PIN_BTN_3MIN);
      lastBtn5 = digitalRead(PIN_BTN_5MIN);
      return;
    }
  }

  // Update last states
  lastBtn1 = btn1;
  lastBtn2 = btn2;

  lastBtn3 = btn3;
  lastBtn5 = btn5;
  delay(10); // Small loop delay
}
