// NUCLEO-L152RE + 16x2 HD44780 LCD (PCF8574 I2C backpack, address 0x27)
// Clock / stopwatch / countdown timer with 5 buttons. Uses only Arduino.h.
//
// Buttons (wired between the pin and GND, internal pull-up):
//   PB0  -> clock mode
//   PB3  -> stopwatch: enter mode and start (also resumes after a stop)
//   PB4  -> stopwatch: stop (press again while stopped = reset to 0)
//   PA6  -> countdown timer: enter / next field (hour -> minute -> start)
//   PA10 -> countdown timer: +1 on the selected field (hold to repeat)
//
// LCD wiring: SDA = D14 (PB9), SCL = D15 (PB8)

#include <Arduino.h>

// ---------- Settings ----------
#define SDA_PIN   D14
#define SCL_PIN   D15
#define LCD_ADDR  0x27

#define BTN_CLOCK     PB0
#define BTN_SW_START  PB3
#define BTN_SW_STOP   PB4
#define BTN_TMR_MODE  PA6
#define BTN_TMR_SET   PA10

// 1 = button connects the pin to GND (pull-up used)
// 0 = button/touch module drives the pin HIGH (pull-down used)
#define ACTIVE_LOW    1
#define DEBOUNCE_MS   30

#define BIT_RS 0x01
#define BIT_RW 0x02
#define BIT_E  0x04
#define BIT_BL 0x08

// ---------- Software I2C (open-drain emulation) ----------
#define I2C_US 5

static inline void sdaHigh() { pinMode(SDA_PIN, INPUT_PULLUP); }
static inline void sdaLow()  { digitalWrite(SDA_PIN, LOW); pinMode(SDA_PIN, OUTPUT); }
static inline void sclHigh() { pinMode(SCL_PIN, INPUT_PULLUP); }
static inline void sclLow()  { digitalWrite(SCL_PIN, LOW); pinMode(SCL_PIN, OUTPUT); }

static void i2cStart() {
  sdaHigh(); sclHigh(); delayMicroseconds(I2C_US);
  sdaLow();  delayMicroseconds(I2C_US);
  sclLow();
}

static void i2cStop() {
  sdaLow();  delayMicroseconds(I2C_US);
  sclHigh(); delayMicroseconds(I2C_US);
  sdaHigh(); delayMicroseconds(I2C_US);
}

static bool i2cWrite(uint8_t b) {
  for (uint8_t i = 0; i < 8; i++) {
    if (b & 0x80) sdaHigh(); else sdaLow();
    delayMicroseconds(I2C_US);
    sclHigh(); delayMicroseconds(I2C_US);
    sclLow();
    b <<= 1;
  }
  sdaHigh();                                   // release for ACK
  delayMicroseconds(I2C_US);
  sclHigh(); delayMicroseconds(I2C_US);
  bool ack = (digitalRead(SDA_PIN) == LOW);
  sclLow();
  return ack;
}

static void pcfWrite(uint8_t v) {
  i2cStart();
  i2cWrite(LCD_ADDR << 1);
  i2cWrite(v);
  i2cStop();
}

// ---------- HD44780 over PCF8574 (4-bit mode) ----------
static uint8_t blBit = BIT_BL;                 // backlight bit (0 = off)

static void lcdWrite4(uint8_t nibble, uint8_t rs) {
  uint8_t d = (nibble << 4) | rs | blBit;
  pcfWrite(d | BIT_E);
  pcfWrite(d & ~BIT_E);
  delayMicroseconds(50);
}

static void lcdSend(uint8_t v, uint8_t rs) {
  lcdWrite4(v >> 4, rs);
  lcdWrite4(v & 0x0F, rs);
}

static void lcdCmd(uint8_t c) {
  lcdSend(c, 0);
  if (c <= 0x03) delay(2);                     // clear / home are slow
}

static void lcdInit() {
  delay(50);
  lcdWrite4(0x03, 0); delay(5);
  lcdWrite4(0x03, 0); delay(1);
  lcdWrite4(0x03, 0); delay(1);
  lcdWrite4(0x02, 0);                          // switch to 4-bit mode
  lcdCmd(0x28);                                // 4-bit, 2 lines, 5x8 font
  lcdCmd(0x0C);                                // display on, cursor off
  lcdCmd(0x01);                                // clear
  lcdCmd(0x06);                                // entry mode: increment
}

static void lcdSetCursor(uint8_t col, uint8_t row) {
  lcdCmd(0x80 | (col + (row ? 0x40 : 0x00)));
}

static void lcdPrint(const char *s) {
  while (*s) lcdSend((uint8_t)*s++, BIT_RS);
}

static void setBacklight(bool on) {
  blBit = on ? BIT_BL : 0;
  pcfWrite(blBit);
}

// Writes a full 16-char line, only if its content changed (no flicker)
static char shown[2][17];

static void lcdLine(uint8_t row, const char *txt) {
  char buf[17];
  size_t n = strlen(txt);
  if (n > 16) n = 16;
  memset(buf, ' ', 16);
  buf[16] = 0;
  memcpy(buf + (16 - n) / 2, txt, n);          // centered
  if (memcmp(buf, shown[row], 17) == 0) return;
  memcpy(shown[row], buf, 17);
  lcdSetCursor(0, row);
  lcdPrint(buf);
}

// ---------- Calendar / clock ----------
static const char *DAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static bool isLeap(uint16_t y) {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static uint8_t daysInMonth(uint8_t m, uint16_t y) {
  static const uint8_t dm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return (m == 2 && isLeap(y)) ? 29 : dm[m - 1];
}

static uint8_t weekday(uint16_t y, uint8_t m, uint8_t d) {   // 0 = Sunday
  static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

uint8_t  hh, mm, ss, day, month;
uint16_t year;

// Start from the PC time at compile time (or assign the values yourself)
static void loadCompileTime() {
  const char *date = __DATE__;                 // "Oct 16 2026"
  const char *time = __TIME__;                 // "12:34:56"
  const char *names = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {date[0], date[1], date[2], 0};
  month = (strstr(names, mon) - names) / 3 + 1;
  day   = atoi(date + 4);
  year  = atoi(date + 7);
  hh = atoi(time);
  mm = atoi(time + 3);
  ss = atoi(time + 6);
}

static void clockTick() {
  if (++ss < 60) return;
  ss = 0;
  if (++mm < 60) return;
  mm = 0;
  if (++hh < 24) return;
  hh = 0;
  if (++day > daysInMonth(month, year)) {
    day = 1;
    if (++month > 12) { month = 1; year++; }
  }
}

// ---------- Buttons ----------
struct Button {
  uint32_t pin;
  bool     raw, state;
  uint32_t changedAt, pressedAt, repeatAt;
};

static bool readPressed(uint32_t pin) {
  return digitalRead(pin) == (ACTIVE_LOW ? LOW : HIGH);
}

static void initButton(Button &b, uint32_t pin) {
  pinMode(pin, ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  b.pin = pin;
  b.raw = b.state = readPressed(pin);          // avoids a fake press at boot
  b.changedAt = b.pressedAt = b.repeatAt = millis();
}

// Returns true once per press (and repeatedly while held, if repeat is set)
static bool pollButton(Button &b, bool repeat) {
  uint32_t now = millis();
  bool r = readPressed(b.pin);
  if (r != b.raw) { b.raw = r; b.changedAt = now; }
  if (now - b.changedAt >= DEBOUNCE_MS && r != b.state) {
    b.state = r;
    if (r) { b.pressedAt = b.repeatAt = now; return true; }
  }
  if (repeat && b.state && now - b.pressedAt >= 500 && now - b.repeatAt >= 120) {
    b.repeatAt = now;
    return true;
  }
  return false;
}

Button btnClock, btnSwStart, btnSwStop, btnTmrMode, btnTmrSet;

// ---------- Modes ----------
enum Mode {
  MODE_CLOCK,
  MODE_STOPWATCH,
  MODE_TIMER_SET_H,
  MODE_TIMER_SET_M,
  MODE_TIMER_RUN,
  MODE_TIMER_DONE
};

Mode mode = MODE_CLOCK;

// Stopwatch
bool     swRunning = false;
uint32_t swStart = 0, swElapsed = 0;

// Countdown timer
uint8_t  tH = 0, tM = 0;
uint32_t timerEnd = 0;

static void handleButtons() {
  bool bClock  = pollButton(btnClock,   false);
  bool bSwGo   = pollButton(btnSwStart, false);
  bool bSwStop = pollButton(btnSwStop,  false);
  bool bTmr    = pollButton(btnTmrMode, false);
  bool bSet    = pollButton(btnTmrSet,  true);

  // Alarm is showing: any button dismisses it
  if (mode == MODE_TIMER_DONE) {
    if (bClock || bSwGo || bSwStop || bTmr || bSet) {
      digitalWrite(LED_BUILTIN, LOW);
      setBacklight(true);
      mode = MODE_CLOCK;
    }
    return;
  }

  if (bClock) {                                // -> clock (cancels stopwatch/timer)
    swRunning = false;
    mode = MODE_CLOCK;
  } else if (bSwGo) {                          // -> stopwatch start / resume
    if (mode != MODE_STOPWATCH) {
      mode = MODE_STOPWATCH;
      swElapsed = 0;
      swRunning = true;
      swStart = millis();
    } else if (!swRunning) {
      swRunning = true;
      swStart = millis();
    }
  } else if (bSwStop) {                        // stopwatch stop / reset
    if (mode == MODE_STOPWATCH) {
      if (swRunning) {
        swElapsed += millis() - swStart;
        swRunning = false;
      } else {
        swElapsed = 0;
      }
    }
  } else if (bTmr) {                           // timer: next step
    switch (mode) {
      case MODE_CLOCK:
      case MODE_STOPWATCH:
      case MODE_TIMER_RUN:                     // restart the setting process
        swRunning = false;
        tH = tM = 0;
        mode = MODE_TIMER_SET_H;
        break;
      case MODE_TIMER_SET_H:
        mode = MODE_TIMER_SET_M;
        break;
      case MODE_TIMER_SET_M:
        if (tH == 0 && tM == 0) {              // nothing entered -> cancel
          mode = MODE_CLOCK;
        } else {
          timerEnd = millis() + (tH * 3600UL + tM * 60UL) * 1000UL;
          mode = MODE_TIMER_RUN;
        }
        break;
      default:
        break;
    }
  } else if (bSet) {                           // timer: +1 on selected field
    if (mode == MODE_TIMER_SET_H)      tH = (tH + 1) % 24;
    else if (mode == MODE_TIMER_SET_M) tM = (tM + 1) % 60;
  }
}

static void render() {
  char a[24], b[24];

  switch (mode) {
    case MODE_CLOCK:
      snprintf(a, sizeof(a), "%02u:%02u:%02u",
               (unsigned)hh, (unsigned)mm, (unsigned)ss);
      snprintf(b, sizeof(b), "%s %02u.%02u.%04u",
               DAYS[weekday(year, month, day)],
               (unsigned)day, (unsigned)month, (unsigned)year);
      break;

    case MODE_STOPWATCH: {
      uint32_t e = swElapsed + (swRunning ? (millis() - swStart) : 0);
      snprintf(a, sizeof(a), "%02lu:%02lu:%02lu.%lu",
               (unsigned long)((e / 3600000UL) % 100),
               (unsigned long)((e / 60000UL) % 60),
               (unsigned long)((e / 1000UL) % 60),
               (unsigned long)((e / 100UL) % 10));
      snprintf(b, sizeof(b), "Stopwatch %s", swRunning ? "RUN" : "STOP");
      break;
    }

    case MODE_TIMER_SET_H:
      strcpy(a, "Set hours");
      snprintf(b, sizeof(b), "[%02u]:%02u ", (unsigned)tH, (unsigned)tM);
      break;

    case MODE_TIMER_SET_M:
      strcpy(a, "Set minutes");
      snprintf(b, sizeof(b), " %02u:[%02u]", (unsigned)tH, (unsigned)tM);
      break;

    case MODE_TIMER_RUN: {
      int32_t rem = (int32_t)(timerEnd - millis());
      if (rem <= 0) {                          // finished
        mode = MODE_TIMER_DONE;
        return;
      }
      uint32_t s = ((uint32_t)rem + 999) / 1000;
      snprintf(a, sizeof(a), "%02lu:%02lu:%02lu",
               (unsigned long)(s / 3600),
               (unsigned long)((s / 60) % 60),
               (unsigned long)(s % 60));
      strcpy(b, "Timer running");
      break;
    }

    case MODE_TIMER_DONE:
    default: {
      strcpy(a, "TIME'S UP!");
      strcpy(b, "Press any button");
      // flash backlight and the on-board LED
      static bool lastOn = true;
      bool on = ((millis() / 500) % 2) == 0;
      if (on != lastOn) {
        lastOn = on;
        digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
        setBacklight(on);
      }
      break;
    }
  }

  lcdLine(0, a);
  lcdLine(1, b);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  initButton(btnClock,   BTN_CLOCK);
  initButton(btnSwStart, BTN_SW_START);
  initButton(btnSwStop,  BTN_SW_STOP);
  initButton(btnTmrMode, BTN_TMR_MODE);
  initButton(btnTmrSet,  BTN_TMR_SET);

  sdaHigh();
  sclHigh();
  delay(100);
  lcdInit();
  loadCompileTime();
}

void loop() {
  static uint32_t lastTick = millis();

  // The clock keeps running in the background in every mode
  while (millis() - lastTick >= 1000) {
    lastTick += 1000;
    clockTick();
  }

  handleButtons();
  render();
}