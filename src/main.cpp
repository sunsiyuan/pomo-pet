#include <M5StickCPlus.h>
#include <Preferences.h>
#include <initializer_list>

TFT_eSprite screen(&M5.Lcd);
Preferences prefs;
enum Mode { READY, FOCUS, REST, CELEBRATE };
Mode mode = READY;
bool paused = false, muted = false, longA = false, longB = false;
bool swallowA = false, swallowB = false, dimmed = false;
struct Preset { uint32_t focus, shortRest, longRest; uint8_t rounds; };
const Preset PRESETS[] = {{50 * 60, 10 * 60, 20 * 60, 2}, {25 * 60, 5 * 60, 15 * 60, 4}};
uint8_t preset = 0;
const Preset& P() { return PRESETS[preset]; }
uint32_t restSeconds();
uint32_t remaining = PRESETS[0].focus, lastTick = 0, celebrationAt = 0, lastActive = 0;
uint32_t total = 0, today = 0, todayKey = 0;
uint8_t roundCount = 0;
constexpr int BRIGHT = 80, DIM = 30;  // M5StickCPlus 0.1.1 brightness scale is 0-100.
constexpr uint32_t DIM_AFTER_MS = 30000;
// IMU axes as mounted: +Z out of the screen, X across the screen width.
constexpr float FACE_SIGN = 1.0f, TILT_SIGN = -1.0f;
float accX = 0, accY = 0, accZ = 0, tiltX = 0;  // tiltX: smoothed slide in px.
bool faceDown = false, ledAlert = false;
uint32_t flipSince = 0;
const uint16_t BG = 0xF75B, INK = 0x31A6, CLAY = 0xDC0D,
               SHADE = 0xBB2B, SOFT = 0xE6B7, SAGE = 0x73AE;
float batVolts = 4.0f;
bool usbPower = false;

// Non-blocking tunes: each note is {Hz, ms}; 0 Hz is a gap.
struct Note { uint16_t hz, ms; };
const Note TUNE_START[] = {{880, 70}, {0, 40}, {1320, 110}};
const Note TUNE_PAUSE[] = {{1320, 70}, {0, 40}, {880, 90}};
const Note TUNE_DONE[] = {{1047, 120}, {1319, 120}, {1568, 120}, {2093, 260}};
const Note TUNE_REST[] = {{784, 180}};
const Note TUNE_WAKE[] = {{1568, 90}, {0, 90}, {1568, 90}, {0, 90}, {1568, 160}};
const Note TUNE_RESET[] = {{523, 140}};
const Note TUNE_CLICK[] = {{1000, 80}};
const Note* tune = nullptr;
uint8_t tuneLen = 0, tuneAt = 0;
uint32_t noteEnd = 0;
template <size_t N> void play(const Note (&t)[N]) {
  if (muted) return;
  tune = t; tuneLen = N; tuneAt = 0; noteEnd = 0;
}
void updateTune(uint32_t now) {
  if (!tune || (int32_t)(now - noteEnd) < 0) return;
  if (tuneAt >= tuneLen) { tune = nullptr; M5.Beep.mute(); return; }
  const Note& n = tune[tuneAt++];
  if (n.hz) M5.Beep.tone(n.hz); else M5.Beep.mute();
  noteEnd = now + n.ms;
}
// RTC date as yyyymmdd, or 0 if the clock was never set or lost power.
uint32_t dateKey() {
  Wire1.beginTransmission(0x51);
  Wire1.write(0x02);
  Wire1.endTransmission();
  Wire1.requestFrom(0x51, 1);
  if (!Wire1.available() || Wire1.read() & 0x80) return 0;  // VL: clock integrity lost.
  RTC_DateTypeDef d;
  M5.Rtc.GetDate(&d);
  if (d.Year < 2026) return 0;
  return d.Year * 10000UL + d.Month * 100 + d.Date;
}
// Zero today's count once the date rolls over.
void refreshDay() {
  uint32_t key = dateKey();
  if (!key || key == todayKey) return;
  todayKey = key; today = 0;
  prefs.putUInt("day", todayKey); prefs.putUInt("today", today);
}
// Mac sends "T2026-10-22 14:03:05" after each upload (tools/settime.py).
void readSerialTime() {
  static char line[32];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && len < sizeof(line) - 1) { line[len++] = c; continue; }
    line[len] = 0; len = 0;
    if (!strcmp(line, "I")) {  // Axis calibration readout.
      Serial.printf("CLAWD_IMU ax=%.2f ay=%.2f az=%.2f\n", accX, accY, accZ);
      continue;
    }
    int y, mo, d, h, mi, se;
    if (sscanf(line, "T%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) continue;
    RTC_DateTypeDef date = {0, (uint8_t)mo, (uint8_t)d, (uint16_t)y};
    RTC_TimeTypeDef time = {(uint8_t)h, (uint8_t)mi, (uint8_t)se};
    M5.Rtc.SetDate(&date);
    M5.Rtc.SetTime(&time);  // Writing seconds also clears the VL flag.
    refreshDay();
    Serial.printf("CLAWD_TIME_SET %s today=%lu\n", line + 1, (unsigned long)today);
  }
}
// Progress survives power-off; restored timers come back paused.
void saveState() {
  prefs.putUChar("mode", mode);
  prefs.putUInt("remain", remaining);
  prefs.putUChar("round", roundCount);
}
void wake(uint32_t now) {
  lastActive = now;
  if (dimmed) { dimmed = false; M5.Axp.ScreenBreath(BRIGHT); }
}
void readPower() {
  batVolts = M5.Axp.GetBatVoltage();
  usbPower = M5.Axp.GetVBusVoltage() > 4.0f;
}

void text(const char* s, int y, int font = 2, uint16_t c = INK) {
  screen.setTextColor(c, BG);
  screen.drawCentreString(s, 67, y, font);
}
void pixel(int x, int y, int w, int h, uint16_t c, int ox, int oy) {
  screen.fillRect(ox + x * 7, oy + y * 7, w * 7, h * 7, c);
}
// Each character draws its body and face; desk props, shadow and effects are shared.
struct Pose { int ox, oy, look; bool sleeping, happy, blink; };
struct Character { const char* title; uint16_t hand; void (*draw)(const Pose&); };
#include "chars/clawd.h"
// Fan skins are optional: a public checkout without them still builds.
#if __has_include("chars/pikachu.h")
#include "chars/pikachu.h"
#define PIKACHU_ENTRY &PIKACHU,
#else
#define PIKACHU_ENTRY
#endif
#if __has_include("chars/gengar.h")
#include "chars/gengar.h"
#define GENGAR_ENTRY &GENGAR,
#else
#define GENGAR_ENTRY
#endif
#if __has_include("chars/snorlax.h")
#include "chars/snorlax.h"
#define SNORLAX_ENTRY &SNORLAX,
#else
#define SNORLAX_ENTRY
#endif
#if __has_include("chars/totoro.h")
#include "chars/totoro.h"
#define TOTORO_ENTRY &TOTORO,
#else
#define TOTORO_ENTRY
#endif
const Character* CHARACTERS[] = {&CLAWD, PIKACHU_ENTRY GENGAR_ENTRY SNORLAX_ENTRY TOTORO_ENTRY};
constexpr uint8_t CHARACTER_COUNT = sizeof(CHARACTERS) / sizeof(CHARACTERS[0]);
uint8_t character = 0;
const Character& chr() { return *CHARACTERS[character]; }
void pet(uint32_t now) {
  bool sleeping = mode == REST;
  bool happy = mode == CELEBRATE;
  int bounce = happy ? -abs((int)(now / 60 % 12) - 6) : 0;
  // Off the desk props the pet slides toward the low side; eyes always look there.
  int slide = mode == FOCUS ? 0 : (int)tiltX;
  int look = constrain((int)(tiltX / 5), -2, 2);
  int ox = 15 + slide, oy = 78 + bounce + (!sleeping && now / 700 % 2 ? 1 : 0);
  screen.fillRoundRect(32 + slide, 142, 72, 4, 2, SOFT);
  chr().draw({ox, oy, look, sleeping, happy, now % 5300 > 5120});
  if (mode == FOCUS) {
    // Rotate every 45 focused seconds; pauses keep the current prop.
    uint8_t activity = (total + (P().focus - remaining) / 45) % 3;
    uint32_t motion = paused ? 0 : now;
    if (activity == 0) {
      screen.fillRect(44, 119, 49, 28, 0xA4B2);
      screen.fillRect(47, 122, 43, 20, 0xCE17);
      screen.fillRect(39, 147, 59, 4, 0x7B8E);
      screen.fillRect(64, 129, 8, 5, SOFT);
      screen.fillRect(motion / 180 % 2 ? 39 : 92, 135, 7, 5, chr().hand);
    } else if (activity == 1) {
      // Open sage book, cream pages, and an occasional page turn.
      screen.fillRect(37, 121, 62, 31, SAGE);
      screen.fillRect(40, 118, 26, 30, 0xFF9D);
      screen.fillRect(69, 118, 27, 30, 0xFF9D);
      screen.fillRect(66, 119, 3, 31, 0xB5D4);
      for (int y : {125, 131, 137}) {
        screen.fillRect(44, y, 17, 2, 0xC638);
        screen.fillRect(74, y, 17, 2, 0xC638);
      }
      if (motion / 600 % 12 == 11)
        screen.fillTriangle(68, 118, 88, 113, 68, 148, SOFT);
      screen.fillRect(33, 134, 8, 7, chr().hand);
      screen.fillRect(95, 134, 8, 7, chr().hand);
    } else {
      // Notebook and a pencil moving across a line.
      screen.fillRect(43, 120, 48, 32, SAGE);
      screen.fillRect(47, 117, 40, 32, 0xFF9D);
      for (int y : {123, 130, 137, 144}) {
        screen.fillRect(44, y, 5, 2, INK);
        screen.fillRect(53, y, 27, 1, SOFT);
      }
      int dx = (motion / 250 % 4) * 2;
      screen.fillRect(54, 127, 12 + dx, 2, INK);
      for (int i = 0; i < 5; ++i)
        screen.fillRect(66 + dx + i * 2, 126 - i * 2, 4, 4, 0xE589);
      screen.fillRect(64 + dx, 135, 7, 5, chr().hand);
      screen.fillRect(36, 136, 10, 6, chr().hand);
    }
  }
  if (sleeping) text(now / 850 % 2 ? "z z" : "z", 56, 2, SAGE);
  if (happy) {
    for (int i = 0; i < 8; ++i) {
      int x = (i * 41 + 7) % 125;
      int y = 48 + (now / 50 + i * 19) % 100;
      screen.fillRect(x, y, 3, 4, i % 2 ? CLAY : SAGE);
    }
  }
}
void render(uint32_t now) {
  screen.fillSprite(BG);
  screen.setTextDatum(TL_DATUM);
  text(chr().title, 9, 1);
  // Battery: four coarse bars from voltage (3.45 V empty .. 4.1 V full).
  // On USB the bars cycle in green as a charging animation.
  int bars = usbPower ? now / 500 % 5
                      : constrain((int)((batVolts - 3.45f) / 0.65f * 4 + 0.5f), 0, 4);
  bool low = !usbPower && batVolts < 3.55f;
  uint16_t batC = low ? CLAY : usbPower ? SAGE : INK;
  if (!low || now / 500 % 2) {
    screen.drawRect(107, 8, 18, 9, batC);
    screen.fillRect(125, 10, 2, 5, batC);
    for (int i = 0; i < bars; ++i) screen.fillRect(109 + i * 4, 10, 3, 5, batC);
  }
  if (muted) {  // Speaker with a slash.
    screen.fillRect(9, 10, 3, 5, INK);
    screen.fillTriangle(11, 12, 16, 7, 16, 17, INK);
    screen.drawLine(8, 17, 19, 6, CLAY);
  }
  screen.drawFastHLine(12, 27, 111, SOFT);
  const char* label = mode == READY ? "let's focus" : mode == FOCUS ?
    (paused ? "take your time" : "one thing at a time") : mode == REST ?
    (paused ? "rest paused" : "a little rest") : "you did it!";
  text(label, 37, 1);
  pet(now);
  char buf[24];
  snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(remaining / 60),
           (unsigned long)(remaining % 60));
  text(buf, 162, 4);
  for (int i = 0; i < P().rounds; ++i)
    screen.fillCircle(67 - (P().rounds - 1) * 7 + i * 14, 202, 3,
                      i < roundCount ? CLAY : SOFT);
  if (todayKey)
    snprintf(buf, sizeof(buf), "%lu today / %lu all", (unsigned long)today, (unsigned long)total);
  else
    snprintf(buf, sizeof(buf), "%lu tomatoes", (unsigned long)total);
  text(buf, 212, 1);
  const char* hint = mode != READY ? "A pause / hold reset"
                   : now / 3000 % 2 ? "hold B: 25 or 50" : "A start / B sound";
  text(hint, 229, 1);
  screen.pushSprite(0, 0);
}
uint32_t restSeconds() {
  return roundCount >= P().rounds ? P().longRest : P().shortRest;
}
void startOrToggle(uint32_t now) {
  if (mode == READY) { mode = FOCUS; paused = false; lastTick = now; play(TUNE_START); }
  else if (mode != CELEBRATE) {
    paused = !paused; lastTick = now;
    if (paused) play(TUNE_PAUSE); else play(TUNE_START);
  }
  saveState();
}
// Face down for 1 s: backlight off, start/resume focus. Face up again: pause.
void readImu(uint32_t now) {
  M5.Imu.getAccelData(&accX, &accY, &accZ);
  float target = constrain(accX * TILT_SIGN * 30, -14.0f, 14.0f);
  tiltX += (target - tiltX) * 0.25f;
  bool down = accZ * FACE_SIGN < -0.75f;
  if (down == faceDown) { flipSince = 0; return; }
  if (!flipSince) flipSince = now;
  if (now - flipSince < 1000) return;
  faceDown = down; flipSince = 0; ledAlert = false;
  if (faceDown) {
    M5.Axp.SetLDO2(false);
    if (mode == READY || (mode == FOCUS && paused)) startOrToggle(now);
  } else {
    M5.Axp.SetLDO2(true);
    dimmed = true; wake(now);
    if (mode == FOCUS && !paused) startOrToggle(now);
  }
}
void reset() {
  mode = READY; paused = false; remaining = P().focus;
  saveState();
}
void setup() {
  M5.begin();
  M5.Lcd.setRotation(0);
  M5.Axp.ScreenBreath(BRIGHT);
  screen.setColorDepth(16);
  if (!screen.createSprite(135, 240)) {
    M5.Lcd.fillScreen(TFT_BLACK); M5.Lcd.println("Display allocation failed");
    while (true) delay(1000);
  }
  prefs.begin("clawd-pomo", false);
  total = prefs.getUInt("total", 0);
  muted = prefs.getBool("muted", false);
  preset = prefs.getUChar("preset", 0) % 2;
  character = prefs.getUChar("char", 0) % CHARACTER_COUNT;
  today = prefs.getUInt("today", 0);
  todayKey = prefs.getUInt("day", 0);
  refreshDay();
  if (!dateKey()) todayKey = 0;  // Clock unknown: show the all-time count only.
  roundCount = prefs.getUChar("round", 0);
  mode = (Mode)prefs.getUChar("mode", READY);
  remaining = prefs.getUInt("remain", P().focus);
  if (mode == CELEBRATE) { mode = REST; remaining = restSeconds(); }
  if (mode == READY || mode > CELEBRATE) { mode = READY; remaining = P().focus; }
  else paused = true;
  lastTick = lastActive = millis();
  readPower();
  M5.Imu.Init();
  pinMode(M5_LED, OUTPUT);
  digitalWrite(M5_LED, HIGH);  // Red LED is active-low.
  Serial.printf("CLAWD_POMO_READY preset=%lu/%lu rounds=%u mode=%d remain=%lu date=%lu\n",
                (unsigned long)P().focus / 60, (unsigned long)P().shortRest / 60, P().rounds,
                mode, (unsigned long)remaining, (unsigned long)dateKey());
}
void loop() {
  M5.update();
  uint32_t now = millis();
  // While dimmed, a press only wakes the screen.
  if (M5.BtnA.wasPressed()) { swallowA = dimmed; wake(now); ledAlert = false; }
  if (M5.BtnB.wasPressed()) { swallowB = dimmed; wake(now); ledAlert = false; }
  // A+B together: next character. Both releases are swallowed.
  static bool combo = false;
  if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
    if (!combo && !swallowA && !swallowB) {
      character = (character + 1) % CHARACTER_COUNT;
      prefs.putUChar("char", character); play(TUNE_CLICK);
    }
    combo = swallowA = swallowB = true;
  } else if (!M5.BtnA.isPressed() && !M5.BtnB.isPressed()) combo = false;
  if (M5.BtnA.pressedFor(1500) && !longA && !swallowA) { reset(); longA = true; play(TUNE_RESET); }
  if (M5.BtnA.wasReleased()) {
    if (!longA && !swallowA) startOrToggle(now);
    longA = swallowA = false;
  }
  if (M5.BtnB.pressedFor(1500) && !longB && !swallowB) {
    longB = true;
    if (mode == READY) {
      preset = 1 - preset; prefs.putUChar("preset", preset);
      roundCount = 0; reset(); play(TUNE_CLICK);
    }
  }
  if (M5.BtnB.wasReleased()) {
    if (!longB && !swallowB) {
      muted = !muted; prefs.putBool("muted", muted);
      if (muted) { tune = nullptr; M5.Beep.mute(); } else play(TUNE_CLICK);
    }
    longB = swallowB = false;
  }
  // Power key: short press wakes; the 1 s long-press flag fires before the
  // 4 s hardware shutdown, so save progress there.
  static uint32_t lastPek = 0;
  if (now - lastPek >= 100) {
    lastPek = now;
    uint8_t pek = M5.Axp.GetBtnPress();
    if (pek & 0x02) wake(now);
    if (pek & 0x01) saveState();
  }
  if ((mode == FOCUS || mode == REST) && !paused) {
    uint32_t elapsed = (now - lastTick) / 1000;
    if (elapsed) {
      lastTick += elapsed * 1000;
      remaining = elapsed >= remaining ? 0 : remaining - elapsed;
      if (!remaining) {
        if (mode == FOCUS) {
          play(TUNE_DONE);
          refreshDay();
          ++total; ++today; ++roundCount;
          prefs.putUInt("total", total); prefs.putUInt("today", today);
          mode = CELEBRATE; celebrationAt = now;
        } else {
          if (roundCount >= P().rounds) roundCount = 0;
          mode = READY; remaining = P().focus;
          play(TUNE_WAKE);
          ledAlert = muted;  // Silent mode: blink until someone responds.
        }
        saveState(); wake(now);
      }
    }
  }
  if (mode == CELEBRATE && now - celebrationAt >= 6000) {
    mode = REST;
    remaining = restSeconds();
    lastTick = now;
    play(TUNE_REST);
    saveState();
  }
  static uint32_t lastSave = 0;
  if ((mode == FOCUS || mode == REST) && !paused && now - lastSave >= 60000) {
    lastSave = now; saveState();
  }
  if (!dimmed && mode != CELEBRATE && now - lastActive >= DIM_AFTER_MS) {
    dimmed = true; M5.Axp.ScreenBreath(DIM);
  }
  static uint32_t lastPower = 0;
  if (now - lastPower >= 2000) { lastPower = now; readPower(); refreshDay(); }
  readSerialTime();
  updateTune(now);
  static uint32_t lastImu = 0;
  if (now - lastImu >= 50) { lastImu = now; readImu(now); }
  bool led = muted && (ledAlert || mode == CELEBRATE) && now % 1000 < 150;
  digitalWrite(M5_LED, led ? LOW : HIGH);
  // Dimmed: 5 fps instead of 15 to cut SPI and CPU time.
  static uint32_t lastFrame = 0;
  if (!faceDown && now - lastFrame >= (dimmed ? 200u : 66u)) { lastFrame = now; render(now); }
  delay(5);
}
