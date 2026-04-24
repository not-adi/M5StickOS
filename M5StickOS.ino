// ============================================================
// M5StickOS — Modern OS for M5StickC Plus2
// Board: M5StickC Plus2
// Libraries: M5Unified, M5GFX, IRremoteESP8266
// Theme: Warm Dark + Teal Accents
// ============================================================

#include <M5Unified.h>
#include <IRsend.h>
#include <WiFi.h>

// ── IR Setup ───────────────────────────────────────────────
const uint16_t IR_PIN = 19;
IRsend irsend(IR_PIN);

// ── Color Palette ──────────────────────────────────────────
#define COL_BG      0x0000  // True Black
#define COL_TEXT    0xF79E  // Warm White (#F0E6D3)
#define COL_MUTED   0x7BEF  // Warm Grey
#define COL_TEAL    0x4F9A  // Teal/Mint accent
#define COL_AMBER   0xE4E0  // Soft Amber (battery)
#define COL_CARD    0x18E3  // Dark card background
#define COL_DKGREY  0x2104  // Darker grey for borders

// ── Sprite (off-screen buffer — zero flicker) ──────────────
M5Canvas canvas(&M5.Display);

// ── State Machine ──────────────────────────────────────────
enum AppState {
  STATE_BOOT, STATE_HOME, STATE_MENU,
  STATE_LEVELER, STATE_SOUND, STATE_WIFI,
  STATE_IR_REMOTE, STATE_FLASHLIGHT,
  STATE_STOPWATCH, STATE_SYSINFO, STATE_SETTINGS
};
AppState currentState = STATE_BOOT;

// ── Menu Config ────────────────────────────────────────────
#define MENU_COUNT 8
int menuIndex = 0;

struct MenuItem {
  const char* name;
  const char* desc;
  const char* icon;
  AppState    state;
};

MenuItem menuItems[MENU_COUNT] = {
  {"Leveler",      "IMU Bubble Level",    "[+]",  STATE_LEVELER},
  {"Sound Meter",  "Mic dB Monitor",      "~",    STATE_SOUND},
  {"Wi-Fi Scan",   "Network Scanner",     "(())", STATE_WIFI},
  {"IR Remote",    "TV Controller",       ">|",   STATE_IR_REMOTE},
  {"Flashlight",   "Max Brightness",      "*",    STATE_FLASHLIGHT},
  {"Stopwatch",    "Timer",               "00",   STATE_STOPWATCH},
  {"System Info",  "Battery & RAM",       "i",    STATE_SYSINFO},
  {"Settings",     "Brightness",          "=",    STATE_SETTINGS}
};

// ── Button Debounce ────────────────────────────────────────
unsigned long btnBpressTime = 0;
bool btnBheld = false;

// ── RTC time struct ────────────────────────────────────────
m5::rtc_time_t rtcTime;
m5::rtc_date_t rtcDate;

// ── Timing ─────────────────────────────────────────────────
unsigned long lastDraw  = 0;
unsigned long bootStart = 0;
const char* dayNames[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
const char* monNames[] = {"Jan","Feb","Mar","Apr","May","Jun",
                           "Jul","Aug","Sep","Oct","Nov","Dec"};

// ── Stopwatch ──────────────────────────────────────────────
bool swRunning = false;
unsigned long swStart = 0;
unsigned long swElapsed = 0;

// ── Flashlight ─────────────────────────────────────────────
bool flashOn = false;

// ── Settings ───────────────────────────────────────────────
int brightness = 200;

// ── Wi-Fi Scanner ──────────────────────────────────────────
#define WIFI_MAX 10
int wifiCount = 0;
int wifiScroll = 0;
bool wifiScanned = false;
String wifiSSIDs[WIFI_MAX];
int32_t wifiRSSI[WIFI_MAX];

// ── Sound Meter ────────────────────────────────────────────
int16_t micBuf[256];
int soundLevel = 0;
int soundPeak = 0;
unsigned long peakTime = 0;

// ── IR Remote ──────────────────────────────────────────────
#define IR_BRAND_COUNT 3
#define IR_CMD_COUNT   6
int irBrand = 0;
int irCmd   = 0;
bool irInControl = false;  // false = brand select, true = command select

const char* irBrandNames[IR_BRAND_COUNT] = {"Samsung", "LG", "Sony"};
const char* irCmdNames[IR_CMD_COUNT] = {"Power", "Vol +", "Vol -", "Ch +", "Ch -", "Mute"};

// NEC codes for Samsung & LG, Sony protocol for Sony
const uint32_t irSamsung[IR_CMD_COUNT] = {
  0xE0E040BF, 0xE0E0E01F, 0xE0E0D02F,
  0xE0E048B7, 0xE0E008F7, 0xE0E0F00F
};
const uint32_t irLG[IR_CMD_COUNT] = {
  0x20DF10EF, 0x20DF40BF, 0x20DFC03F,
  0x20DF00FF, 0x20DF807F, 0x20DF906F
};
const uint16_t irSony[IR_CMD_COUNT] = {
  0xA90, 0x490, 0xC90, 0x090, 0x890, 0x290
};

// ──────────────────────────────────────────────────────────
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setRotation(1);          // Landscape (240×135)
  M5.Display.setBrightness(brightness);

  canvas.createSprite(M5.Display.width(), M5.Display.height());

  irsend.begin();  // Initialize IR transmitter

  // ── Initialize RTC to IST if not set ──
  m5::rtc_date_t d;
  M5.Rtc.getDate(&d);
  if (d.year < 2026) {
    d.year = 2026;
    d.month = 4;
    d.date = 23;
    d.weekDay = 4; // Thursday
    M5.Rtc.setDate(&d);

    m5::rtc_time_t t;
    t.hours = 14;
    t.minutes = 30;
    t.seconds = 0;
    M5.Rtc.setTime(&t);
  }

  bootStart    = millis();
  currentState = STATE_BOOT;
}

// ──────────────────────────────────────────────────────────
void loop() {
  M5.update();
  unsigned long now = millis();

  // ── Always read RTC ──
  M5.Rtc.getTime(&rtcTime);
  M5.Rtc.getDate(&rtcDate);

  // ── Global: Long-press BtnB → Home ──
  if (M5.BtnB.isPressed()) {
    if (btnBpressTime == 0) btnBpressTime = now;
    if (!btnBheld && (now - btnBpressTime > 800)) {
      btnBheld = true;
      if (currentState != STATE_HOME && currentState != STATE_BOOT) {
        currentState = STATE_HOME;
        lastDraw = 0;
      }
    }
  }
  if (M5.BtnB.wasReleased()) {
    btnBpressTime = 0;
    btnBheld = false;
  }

  // ── State Router ──
  switch (currentState) {
    case STATE_BOOT:
      drawBoot(now);
      if (now - bootStart > 2000) currentState = STATE_HOME;
      break;

    case STATE_HOME:
      handleHomeInput();
      if (now - lastDraw > 500) {
        drawHome();
        lastDraw = now;
      }
      break;

    case STATE_MENU:
      handleMenuInput();
      if (now - lastDraw > 100) {
        drawMenu();
        lastDraw = now;
      }
      break;

    case STATE_LEVELER:
      if (now - lastDraw > 50) { drawLeveler(); lastDraw = now; }
      break;

    case STATE_SOUND:
      if (now - lastDraw > 60) { drawSoundMeter(); lastDraw = now; }
      break;

    case STATE_WIFI:
      handleWifiInput();
      if (now - lastDraw > 200) { drawWifiScan(); lastDraw = now; }
      break;

    case STATE_IR_REMOTE:
      handleIRInput();
      if (now - lastDraw > 100) { drawIRRemote(); lastDraw = now; }
      break;

    case STATE_FLASHLIGHT:
      handleFlashlight();
      break;

    case STATE_STOPWATCH:
      handleStopwatchInput();
      if (now - lastDraw > 30) { drawStopwatch(); lastDraw = now; }
      break;

    case STATE_SYSINFO:
      if (now - lastDraw > 500) { drawSysInfo(); lastDraw = now; }
      break;

    case STATE_SETTINGS:
      handleSettingsInput();
      if (now - lastDraw > 100) { drawSettings(); lastDraw = now; }
      break;
  }
}

// ══════════════════════════════════════════════════════════
//  DRAW: Boot Screen
// ══════════════════════════════════════════════════════════
void drawBoot(unsigned long now) {
  float progress = (float)(now - bootStart) / 2000.0f;
  if (progress > 1.0f) progress = 1.0f;

  canvas.fillSprite(COL_BG);

  // Teal horizontal rule fading in
  int barW = (int)(M5.Display.width() * progress);
  canvas.fillRect(0, M5.Display.height()/2 - 1, barW, 2, COL_TEAL);

  // Title
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(COL_TEXT);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextSize(1);
  canvas.drawString("M5StickOS", M5.Display.width()/2, M5.Display.height()/2 - 16);

  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(COL_TEAL);
  canvas.drawString("Loading...", M5.Display.width()/2, M5.Display.height()/2 + 14);

  canvas.pushSprite(0, 0);
  delay(16);
}

// ══════════════════════════════════════════════════════════
//  DRAW: Status Bar (top 16px)
// ══════════════════════════════════════════════════════════
void drawStatusBar() {
  int W = M5.Display.width();

  // Background strip
  canvas.fillRect(0, 0, W, 16, 0x1082); // Very dark grey strip

  // ── Battery ──
  int batPct = M5.Power.getBatteryLevel();
  bool charging = M5.Power.isCharging();
  uint16_t batCol = (batPct < 20) ? TFT_RED : COL_AMBER;

  canvas.setTextDatum(TL_DATUM);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(batCol);
  canvas.setTextSize(1);

  char batBuf[12];
  if (charging) snprintf(batBuf, sizeof(batBuf), "+%d%%", batPct);
  else          snprintf(batBuf, sizeof(batBuf), "%d%%",  batPct);
  canvas.drawString(batBuf, 4, 4);

  // ── Time (HH:MM) ──
  char timeBuf[6];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", rtcTime.hours, rtcTime.minutes);
  canvas.setTextColor(COL_MUTED);
  canvas.setTextDatum(TR_DATUM);
  canvas.drawString(timeBuf, W - 4, 4);
}

// ══════════════════════════════════════════════════════════
//  DRAW: Home Screen (Clock Face)
// ══════════════════════════════════════════════════════════
void drawHome() {
  int W = M5.Display.width();   // 240
  int H = M5.Display.height();  // 135

  canvas.fillSprite(COL_BG);
  drawStatusBar();

  int centerY = (H + 16) / 2;   // vertical center below status bar

  // ── Large Time HH:MM ──
  char timeBuf[6];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", rtcTime.hours, rtcTime.minutes);

  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(COL_TEXT);
  canvas.setFont(&fonts::FreeSansBold24pt7b);
  canvas.setTextSize(1);
  canvas.drawString(timeBuf, W/2, centerY - 10);

  // ── Seconds ──
  char secBuf[4];
  snprintf(secBuf, sizeof(secBuf), ":%02d", rtcTime.seconds);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(COL_MUTED);
  canvas.drawString(secBuf, W/2, centerY + 22);

  // ── Date ──
  char dateBuf[20];
  snprintf(dateBuf, sizeof(dateBuf), "%s, %s %02d",
    dayNames[rtcDate.weekDay],
    monNames[rtcDate.month - 1],
    rtcDate.date);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(COL_TEAL);
  canvas.drawString(dateBuf, W/2, centerY + 40);

  // ── Teal divider above date ──
  canvas.drawFastHLine(W/2 - 40, centerY + 30, 80, COL_TEAL);

  canvas.pushSprite(0, 0);
}

// ══════════════════════════════════════════════════════════
//  INPUT: Home Screen — BtnA or BtnB opens menu
// ══════════════════════════════════════════════════════════
void handleHomeInput() {
  if (M5.BtnA.wasClicked() || (M5.BtnB.wasClicked() && !btnBheld)) {
    currentState = STATE_MENU;
    menuIndex = 0;
    lastDraw = 0;
  }
}

// ══════════════════════════════════════════════════════════
//  INPUT: Menu — BtnB scrolls, BtnA selects
// ══════════════════════════════════════════════════════════
void handleMenuInput() {
  if (M5.BtnB.wasClicked() && !btnBheld) {
    menuIndex = (menuIndex + 1) % MENU_COUNT;
    lastDraw = 0;
  }
  if (M5.BtnA.wasClicked()) {
    currentState = menuItems[menuIndex].state;
    lastDraw = 0;
  }
}

// ══════════════════════════════════════════════════════════
//  DRAW: Menu — Card Carousel (one app at a time)
// ══════════════════════════════════════════════════════════
void drawMenu() {
  int W = M5.Display.width();
  int H = M5.Display.height();

  canvas.fillSprite(COL_BG);
  drawStatusBar();

  // ── Page indicator (e.g. 3/8) ──
  char pageBuf[8];
  snprintf(pageBuf, sizeof(pageBuf), "%d/%d", menuIndex + 1, MENU_COUNT);
  canvas.setTextDatum(MC_DATUM);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(COL_MUTED);
  canvas.drawString(pageBuf, W/2, 24);

  // ── Card background ──
  int cardX = 20;
  int cardY = 32;
  int cardW = W - 40;
  int cardH = 72;
  canvas.fillRoundRect(cardX, cardY, cardW, cardH, 8, COL_CARD);
  canvas.drawRoundRect(cardX, cardY, cardW, cardH, 8, COL_TEAL);

  // ── Icon area ──
  canvas.setTextDatum(MC_DATUM);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(COL_TEAL);
  canvas.drawString(menuItems[menuIndex].icon, W/2, cardY + 22);

  // ── App Name ──
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(COL_TEXT);
  canvas.drawString(menuItems[menuIndex].name, W/2, cardY + 42);

  // ── App Description ──
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(COL_MUTED);
  canvas.drawString(menuItems[menuIndex].desc, W/2, cardY + 58);

  // ── Bottom hints ──
  canvas.setFont(&fonts::Font0);
  canvas.setTextDatum(BL_DATUM);
  canvas.setTextColor(COL_TEAL);
  canvas.drawString("[A] Open", 8, H - 4);
  canvas.setTextDatum(BR_DATUM);
  canvas.setTextColor(COL_MUTED);
  canvas.drawString("[B] Next", W - 8, H - 4);

  // ── Dot indicators ──
  int dotY = H - 16;
  int totalDotsW = MENU_COUNT * 8;
  int dotStartX = (W - totalDotsW) / 2;
  for (int i = 0; i < MENU_COUNT; i++) {
    int dx = dotStartX + i * 8 + 3;
    if (i == menuIndex)
      canvas.fillCircle(dx, dotY, 3, COL_TEAL);
    else
      canvas.fillCircle(dx, dotY, 2, COL_DKGREY);
  }

  canvas.pushSprite(0, 0);
}

// ══════════════════════════════════════════════════════════
//  APP 1: LEVELER (IMU Bubble Level)
// ══════════════════════════════════════════════════════════
void drawLeveler() {
  int W = M5.Display.width();
  int H = M5.Display.height();
  float ax, ay, az;
  M5.Imu.getAccelData(&ax, &ay, &az);

  canvas.fillSprite(COL_BG);
  drawStatusBar();

  // Title
  canvas.setTextDatum(TL_DATUM);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(COL_TEAL);
  canvas.drawString("Leveler", 6, 20);

  // Draw crosshair
  int cx = W / 2;
  int cy = (H + 16) / 2 + 4;
  int radius = 40;
  canvas.drawCircle(cx, cy, radius, COL_DKGREY);
  canvas.drawCircle(cx, cy, radius / 2, COL_DKGREY);
  canvas.drawFastHLine(cx - radius, cy, radius * 2, COL_DKGREY);
  canvas.drawFastVLine(cx, cy - radius, radius * 2, COL_DKGREY);

  // Bubble position (clamp to circle)
  int bx = cx + (int)(ax * radius * 0.9f);
  int by = cy + (int)(ay * radius * 0.9f);
  float dist = sqrt((bx-cx)*(bx-cx) + (by-cy)*(by-cy));
  if (dist > radius - 6) {
    bx = cx + (int)((bx-cx) * (radius-6) / dist);
    by = cy + (int)((by-cy) * (radius-6) / dist);
  }

  // Color: teal if level, amber if tilted
  bool isLevel = (abs(ax) < 0.05f && abs(ay) < 0.05f);
  uint16_t bubCol = isLevel ? COL_TEAL : COL_AMBER;
  canvas.fillCircle(bx, by, 6, bubCol);

  // Degree readout
  char buf[24];
  snprintf(buf, sizeof(buf), "X:%.1f  Y:%.1f", ax * 90.0f, ay * 90.0f);
  canvas.setTextDatum(BC_DATUM);
  canvas.setTextColor(COL_MUTED);
  canvas.drawString(buf, W/2, H - 4);

  canvas.pushSprite(0, 0);
}

// ══════════════════════════════════════════════════════════
//  APP 2: SOUND METER (Mic dB Monitor)
// ══════════════════════════════════════════════════════════
void drawSoundMeter() {
  int W = M5.Display.width();
  int H = M5.Display.height();

  // Read mic
  if (M5.Mic.isEnabled()) {
    M5.Mic.record(micBuf, 256, 16000);
    int32_t peak = 0;
    for (int i = 0; i < 256; i++) {
      int32_t v = abs(micBuf[i]);
      if (v > peak) peak = v;
    }
    soundLevel = map(constrain(peak, 0, 2048), 0, 2048, 0, 100);
    if (soundLevel > soundPeak || millis() - peakTime > 1500) {
      soundPeak = soundLevel;
      peakTime = millis();
    }
  } else {
    M5.Mic.begin();
  }

  canvas.fillSprite(COL_BG);
  drawStatusBar();

  canvas.setTextDatum(TL_DATUM);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(COL_TEAL);
  canvas.drawString("Sound Meter", 6, 20);

  // Volume bar
  int barX = 20, barY = 38, barW = W - 40, barH = 24;
  canvas.drawRoundRect(barX, barY, barW, barH, 4, COL_DKGREY);
  int fillW = (soundLevel * (barW - 4)) / 100;
  uint16_t barCol = (soundLevel < 60) ? COL_TEAL : COL_AMBER;
  if (fillW > 0)
    canvas.fillRoundRect(barX + 2, barY + 2, fillW, barH - 4, 3, barCol);

  // Peak line
  int peakX = barX + 2 + (soundPeak * (barW - 4)) / 100;
  canvas.drawFastVLine(peakX, barY + 1, barH - 2, COL_TEXT);

  // dB number
  char dbBuf[8];
  snprintf(dbBuf, sizeof(dbBuf), "%d%%", soundLevel);
  canvas.setTextDatum(MC_DATUM);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(COL_TEXT);
  canvas.drawString(dbBuf, W/2, 86);

  // Small label
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(COL_MUTED);
  canvas.setTextDatum(BC_DATUM);
  canvas.drawString("Peak hold: 1.5s", W/2, H - 4);

  canvas.pushSprite(0, 0);
}

// ══════════════════════════════════════════════════════════
//  APP 3: WI-FI SCANNER
// ══════════════════════════════════════════════════════════
void handleWifiInput() {
  if (M5.BtnB.wasClicked() && !btnBheld) {
    if (wifiCount > 0) wifiScroll = (wifiScroll + 1) % wifiCount;
    lastDraw = 0;
  }
  if (M5.BtnA.wasClicked()) {
    wifiScanned = false;  // Rescan
    lastDraw = 0;
  }
}

void drawWifiScan() {
  int W = M5.Display.width();
  int H = M5.Display.height();

  if (!wifiScanned) {
    canvas.fillSprite(COL_BG);
    drawStatusBar();
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextColor(COL_TEAL);
    canvas.drawString("Scanning...", W/2, H/2);
    canvas.pushSprite(0, 0);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();
    wifiCount = min(n, WIFI_MAX);
    for (int i = 0; i < wifiCount; i++) {
      wifiSSIDs[i] = WiFi.SSID(i);
      wifiRSSI[i]  = WiFi.RSSI(i);
    }
    wifiScroll = 0;
    wifiScanned = true;
    WiFi.mode(WIFI_OFF);
    return;
  }

  canvas.fillSprite(COL_BG);
  drawStatusBar();

  canvas.setTextDatum(TL_DATUM);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(COL_TEAL);
  char hdr[16];
  snprintf(hdr, sizeof(hdr), "Wi-Fi (%d)", wifiCount);
  canvas.drawString(hdr, 6, 20);

  // Show 4 networks at a time
  int startIdx = (wifiScroll / 4) * 4;
  for (int i = 0; i < 4 && (startIdx + i) < wifiCount; i++) {
    int idx = startIdx + i;
    int y = 34 + i * 22;
    bool selected = (idx == wifiScroll);

    if (selected)
      canvas.fillRoundRect(4, y - 2, W - 8, 20, 4, COL_CARD);

    // SSID (truncate to 16 chars)
    String name = wifiSSIDs[idx].substring(0, 16);
    canvas.setTextColor(selected ? COL_TEXT : COL_MUTED);
    canvas.drawString(name.c_str(), 8, y + 2);

    // Signal bars
    int bars = map(constrain(wifiRSSI[idx], -90, -30), -90, -30, 1, 4);
    for (int b = 0; b < 4; b++) {
      int bx = W - 30 + b * 5;
      int bh = 4 + b * 3;
      uint16_t bc = (b < bars) ? COL_TEAL : COL_DKGREY;
      canvas.fillRect(bx, y + 14 - bh, 3, bh, bc);
    }
  }

  canvas.setTextDatum(BC_DATUM);
  canvas.setTextColor(COL_MUTED);
  canvas.drawString("[A] Rescan  [B] Scroll", W/2, H - 4);

  canvas.pushSprite(0, 0);
}

// ══════════════════════════════════════════════════════════
//  APP 4: IR REMOTE (Samsung / LG / Sony)
// ══════════════════════════════════════════════════════════
void handleIRInput() {
  if (!irInControl) {
    // Brand selection
    if (M5.BtnB.wasClicked() && !btnBheld) {
      irBrand = (irBrand + 1) % IR_BRAND_COUNT;
      lastDraw = 0;
    }
    if (M5.BtnA.wasClicked()) {
      irInControl = true;
      irCmd = 0;
      lastDraw = 0;
    }
  } else {
    // Command selection
    if (M5.BtnB.wasClicked() && !btnBheld) {
      irCmd = (irCmd + 1) % IR_CMD_COUNT;
      lastDraw = 0;
    }
    if (M5.BtnA.wasClicked()) {
      // SEND IR!
      sendIRCode(irBrand, irCmd);
      lastDraw = 0;
    }
  }
}

void sendIRCode(int brand, int cmd) {
  switch (brand) {
    case 0: irsend.sendNEC(irSamsung[cmd], 32); break;
    case 1: irsend.sendNEC(irLG[cmd], 32);      break;
    case 2: irsend.sendSony(irSony[cmd], 12, 2); break;
  }
  // Flash teal confirmation
  M5.Display.fillScreen(COL_TEAL);
  delay(80);
}

void drawIRRemote() {
  int W = M5.Display.width();
  int H = M5.Display.height();

  canvas.fillSprite(COL_BG);
  drawStatusBar();

  canvas.setTextDatum(TL_DATUM);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(COL_TEAL);
  canvas.drawString("IR Remote", 6, 20);

  if (!irInControl) {
    // Brand select screen
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(COL_MUTED);
    canvas.drawString("Select Brand:", W/2, 38);

    for (int i = 0; i < IR_BRAND_COUNT; i++) {
      int y = 54 + i * 20;
      bool sel = (i == irBrand);
      if (sel)
        canvas.fillRoundRect(30, y - 4, W - 60, 18, 4, COL_CARD);
      canvas.setTextDatum(MC_DATUM);
      canvas.setTextColor(sel ? COL_TEAL : COL_MUTED);
      canvas.drawString(irBrandNames[i], W/2, y + 4);
    }

    canvas.setTextDatum(BC_DATUM);
    canvas.setTextColor(COL_MUTED);
    canvas.drawString("[A] Select  [B] Next", W/2, H - 4);
  } else {
    // Command select screen
    canvas.setTextDatum(TR_DATUM);
    canvas.setTextColor(COL_AMBER);
    canvas.drawString(irBrandNames[irBrand], W - 6, 20);

    for (int i = 0; i < IR_CMD_COUNT; i++) {
      int y = 36 + i * 14;
      bool sel = (i == irCmd);
      if (sel)
        canvas.fillRoundRect(20, y - 2, W - 40, 13, 3, COL_CARD);
      canvas.setTextDatum(MC_DATUM);
      canvas.setTextColor(sel ? COL_TEAL : COL_MUTED);
      canvas.drawString(irCmdNames[i], W/2, y + 4);
    }

    canvas.setTextDatum(BC_DATUM);
    canvas.setTextColor(COL_TEAL);
    canvas.drawString("[A] SEND  [B] Next", W/2, H - 4);
  }

  canvas.pushSprite(0, 0);
}

// ══════════════════════════════════════════════════════════
//  APP 5: FLASHLIGHT
// ══════════════════════════════════════════════════════════
void handleFlashlight() {
  if (!flashOn) {
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setBrightness(255);
    flashOn = true;
  }
  if (M5.BtnA.wasClicked()) {
    flashOn = false;
    M5.Display.setBrightness(brightness);
    currentState = STATE_MENU;
    lastDraw = 0;
  }
}

// ══════════════════════════════════════════════════════════
//  APP 6: STOPWATCH
// ══════════════════════════════════════════════════════════
void handleStopwatchInput() {
  if (M5.BtnA.wasClicked()) {
    if (!swRunning) {
      swRunning = true;
      swStart = millis() - swElapsed;
    } else {
      swRunning = false;
      swElapsed = millis() - swStart;
    }
  }
  if (M5.BtnB.wasClicked() && !btnBheld && !swRunning) {
    swElapsed = 0;
    lastDraw = 0;
  }
}

void drawStopwatch() {
  int W = M5.Display.width();
  int H = M5.Display.height();
  unsigned long elapsed = swRunning ? (millis() - swStart) : swElapsed;

  int ms  = (elapsed % 1000) / 10;
  int sec = (elapsed / 1000) % 60;
  int mn  = (elapsed / 60000) % 60;

  canvas.fillSprite(COL_BG);
  drawStatusBar();

  canvas.setTextDatum(TL_DATUM);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(COL_TEAL);
  canvas.drawString("Stopwatch", 6, 20);

  // Large time display
  char timeBuf[12];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d.%02d", mn, sec, ms);
  canvas.setTextDatum(MC_DATUM);
  canvas.setFont(&fonts::FreeSansBold18pt7b);
  canvas.setTextColor(COL_TEXT);
  canvas.drawString(timeBuf, W/2, (H + 16) / 2);

  // Status indicator
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(swRunning ? COL_TEAL : COL_MUTED);
  canvas.setTextDatum(MC_DATUM);
  canvas.drawString(swRunning ? "RUNNING" : "STOPPED", W/2, H - 22);

  // Hints
  canvas.setTextDatum(BC_DATUM);
  canvas.setTextColor(COL_MUTED);
  const char* hint = swRunning ? "[A] Stop" : "[A] Start  [B] Reset";
  canvas.drawString(hint, W/2, H - 4);

  canvas.pushSprite(0, 0);
}

// ══════════════════════════════════════════════════════════
//  APP 7: SYSTEM INFO
// ══════════════════════════════════════════════════════════
void drawSysInfo() {
  int W = M5.Display.width();
  int H = M5.Display.height();

  canvas.fillSprite(COL_BG);
  drawStatusBar();

  canvas.setTextDatum(TL_DATUM);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(COL_TEAL);
  canvas.drawString("System Info", 6, 20);

  int y = 36;
  int lineH = 16;

  // Battery
  int bat = M5.Power.getBatteryLevel();
  char buf[32];
  snprintf(buf, sizeof(buf), "Battery: %d%%", bat);
  canvas.setTextColor(COL_TEXT);
  canvas.drawString(buf, 10, y); y += lineH;

  // Charging
  canvas.setTextColor(COL_MUTED);
  canvas.drawString(M5.Power.isCharging() ? "Status: Charging" : "Status: Discharging", 10, y);
  y += lineH;

  // Free heap
  snprintf(buf, sizeof(buf), "Free RAM: %d KB", ESP.getFreeHeap() / 1024);
  canvas.setTextColor(COL_TEXT);
  canvas.drawString(buf, 10, y); y += lineH;

  // Chip temp (ESP32 internal)
  float temp = temperatureRead();
  snprintf(buf, sizeof(buf), "CPU Temp: %.1f C", temp);
  canvas.drawString(buf, 10, y); y += lineH;

  // Uptime
  unsigned long up = millis() / 1000;
  int uh = up / 3600; int um = (up % 3600) / 60; int us = up % 60;
  snprintf(buf, sizeof(buf), "Uptime: %02d:%02d:%02d", uh, um, us);
  canvas.setTextColor(COL_MUTED);
  canvas.drawString(buf, 10, y);

  canvas.pushSprite(0, 0);
}

// ══════════════════════════════════════════════════════════
//  APP 8: SETTINGS (Brightness)
// ══════════════════════════════════════════════════════════
void handleSettingsInput() {
  if (M5.BtnB.wasClicked() && !btnBheld) {
    brightness += 25;
    if (brightness > 255) brightness = 25;
    M5.Display.setBrightness(brightness);
    lastDraw = 0;
  }
}

void drawSettings() {
  int W = M5.Display.width();
  int H = M5.Display.height();

  canvas.fillSprite(COL_BG);
  drawStatusBar();

  canvas.setTextDatum(TL_DATUM);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(COL_TEAL);
  canvas.drawString("Settings", 6, 20);

  // Brightness label
  canvas.setTextDatum(MC_DATUM);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(COL_TEXT);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", (brightness * 100) / 255);
  canvas.drawString(buf, W/2, 55);

  // Brightness bar
  int barX = 20, barY = 70, barW = W - 40, barH = 16;
  canvas.drawRoundRect(barX, barY, barW, barH, 4, COL_DKGREY);
  int fillW = (brightness * (barW - 4)) / 255;
  canvas.fillRoundRect(barX + 2, barY + 2, fillW, barH - 4, 3, COL_TEAL);

  // Label
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(COL_MUTED);
  canvas.drawString("Brightness", W/2, 98);

  // Hints
  canvas.setTextDatum(BC_DATUM);
  canvas.drawString("[B] Adjust  [Hold B] Home", W/2, H - 4);

  canvas.pushSprite(0, 0);
}

