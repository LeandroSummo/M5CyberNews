/*
 * =====================================================
 *  M5CYBER NEWS v2  -  Multi-feed Italian RSS Reader
 *  Target: M5StickC Plus2  (ESP32-PICO, 240x135 px)
 *
 *  CONTROLS
 *  ─────────────────────────────────────────────────
 *  [News mode]
 *    BtnA  short       -> next article
 *    BtnB  short       -> scroll text / back to top
 *    BtnA  2 sec hold  -> open feed menu
 *    BtnB  4 sec hold  -> WiFi portal
 *
 *  [Feed menu]
 *    BtnB  short       -> cycle feed list (wraps)
 *    BtnA  short       -> SELECT feed + load
 *    BtnA  2 sec hold  -> EXIT menu (no change)
 * =====================================================
 */

#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <vector>

M5Canvas canvas(&StickCP2.Display);

// =====================================================
//  CONFIG
// =====================================================

#define MAX_NEWS          15
#define CHARS_PER_LINE    22
#define VISIBLE_LINES      4

#define REFRESH_INTERVAL  60000UL
#define DIM_TIMEOUT       12000UL
#define SCREEN_TIMEOUT    20000UL
#define BRIGHTNESS_FULL     200
#define BRIGHTNESS_DIM       40
#define MENU_HOLD_MS       2000UL
#define WIFI_HOLD_MS       4000UL

// =====================================================
//  FEED SOURCES
// =====================================================

struct FeedSource {
    const char* name;
    const char* url;
    uint16_t    color;
};

const FeedSource FEEDS[] = {
    { "ANSA",       "https://www.ansa.it/sito/ansait_rss.xml",                 0x07E0 },
    { "CORRIERE",   "https://xml2.corriereobjects.it/rss/homepage.xml",         0xF800 },
    { "REPUBBLICA", "https://www.repubblica.it/rss/homepage/rss2.0.xml",        0x001F },
    { "SOLE 24H",   "https://www.ilsole24ore.com/rss/italia.xml",               0xFD20 },
    { "TGCOM24",    "https://www.tgcom24.mediaset.it/rss/homepage.xml",         0xF81F },
    { "LA STAMPA",  "https://www.lastampa.it/rss/copertina.xml",                0x07FF },
    { "IL FATTO",   "https://www.ilfattoquotidiano.it/feed/",                   0xFFE0 },
};

const int FEED_COUNT = sizeof(FEEDS) / sizeof(FEEDS[0]);

// =====================================================
//  APP STATE
// =====================================================

enum AppMode { MODE_NEWS, MODE_MENU, MODE_LOADING };

AppMode appMode        = MODE_NEWS;
int     currentFeed    = 0;
int     menuCursor     = 0;
bool    ignoreBtnA     = false;

String  newsTitles[MAX_NEWS];
std::vector<String> wrappedLines;

int  totalNews         = 0;
int  currentNews       = 0;
int  currentLineOffset = 0;

bool displaySleeping   = false;
bool displayDimmed     = false;
bool wifiError         = false;

unsigned long lastRefresh     = 0;
unsigned long lastInteraction = 0;

// =====================================================
//  CLEAN RSS TEXT
// =====================================================

String cleanText(String text) {
    text.replace("<![CDATA[", "");
    text.replace("]]>", "");
    text.replace("&amp;",  "&");
    text.replace("&lt;",   "<");
    text.replace("&gt;",   ">");
    text.replace("&quot;", "\"");
    text.replace("&#039;", "'");
    text.replace("&apos;", "'");
    text.replace("&agrave;", "a"); text.replace("&Agrave;", "A");
    text.replace("&egrave;", "e"); text.replace("&Egrave;", "E");
    text.replace("&eacute;", "e"); text.replace("&Eacute;", "E");
    text.replace("&igrave;", "i"); text.replace("&Igrave;", "I");
    text.replace("&ograve;", "o"); text.replace("&Ograve;", "O");
    text.replace("&ugrave;", "u"); text.replace("&Ugrave;", "U");
    text.replace("\xc3\xa0", "a"); text.replace("\xc3\x80", "A");
    text.replace("\xc3\xa8", "e"); text.replace("\xc3\x88", "E");
    text.replace("\xc3\xa9", "e"); text.replace("\xc3\x89", "E");
    text.replace("\xc3\xac", "i"); text.replace("\xc3\x8c", "I");
    text.replace("\xc3\xb2", "o"); text.replace("\xc3\x92", "O");
    text.replace("\xc3\xb9", "u"); text.replace("\xc3\x99", "U");
    text.replace("\xe0", "a"); text.replace("\xe8", "e");
    text.replace("\xe9", "e"); text.replace("\xec", "i");
    text.replace("\xf2", "o"); text.replace("\xf9", "u");
    text.replace("\xe2\x80\x9c", "\"");
    text.replace("\xe2\x80\x9d", "\"");
    text.replace("\xe2\x80\x98", "'");
    text.replace("\xe2\x80\x99", "'");
    text.replace("\xe2\x80\x93", "-");
    text.replace("\xe2\x80\x94", "-");
    text.replace("\xe2\x80\xa6", "...");
    text.replace("\x85", "..."); text.replace("\x96", "-");
    text.replace("\x97", "-");   text.replace("\x91", "'");
    text.replace("\x92", "'");   text.replace("\x93", "\"");
    text.replace("\x94", "\"");
    text.replace("\n", " "); text.replace("\r", " "); text.replace("\t", " ");
    while (text.indexOf("  ") >= 0) text.replace("  ", " ");
    text.trim();
    return text;
}

// =====================================================
//  WORD WRAP
// =====================================================

void wrapText() {
    wrappedLines.clear();
    if (totalNews == 0) {
        wrappedLines.push_back("No news loaded");
        currentLineOffset = 0;
        return;
    }
    String text = newsTitles[currentNews];
    String word = "";
    String line = "";
    text += ' ';
    for (int i = 0; i < (int)text.length(); i++) {
        char c = text[i];
        if (c == ' ') {
            if (word.length() == 0) continue;
            if (line.length() == 0) {
                line = word;
            } else if (line.length() + 1 + word.length() <= CHARS_PER_LINE) {
                line += ' '; line += word;
            } else {
                wrappedLines.push_back(line); line = word;
            }
            word = "";
        } else {
            if (word.length() >= CHARS_PER_LINE) {
                if (line.length() > 0) wrappedLines.push_back(line);
                wrappedLines.push_back(word);
                word = ""; line = "";
            }
            word += c;
        }
    }
    if (line.length() > 0) wrappedLines.push_back(line);
    currentLineOffset = 0;
}

// =====================================================
//  FORWARD DECLARATION
// =====================================================

void renderUI();

// =====================================================
//  SPLASH  -  Pixel-art "NEWS!" drawn with fillRect
//
//  Each letter is a 5-col x 7-row bitmap.
//  Each "pixel" renders as a (PX_W-1) x (PX_H-1) rect
//  leaving a 1-px gap for a clean grid look.
//
//  Bit order per row: bit 4 = leftmost column.
// =====================================================

// --- glyph bitmaps (5 cols x 7 rows) ---------------

const uint8_t G_N[7] = {
    0b10001,   //  #   #
    0b11001,   //  ##  #
    0b10101,   //  # # #
    0b10011,   //  #  ##
    0b10001,   //  #   #
    0b10001,   //  #   #
    0b10001    //  #   #
};
const uint8_t G_E[7] = {
    0b11111,   //  #####
    0b10000,   //  #
    0b10000,   //  #
    0b11110,   //  ####
    0b10000,   //  #
    0b10000,   //  #
    0b11111    //  #####
};
const uint8_t G_W[7] = {
    0b10001,   //  #   #
    0b10001,   //  #   #
    0b10001,   //  #   #
    0b10101,   //  # # #
    0b10101,   //  # # #
    0b11011,   //  ## ##
    0b10001    //  #   #
};
const uint8_t G_S[7] = {
    0b01111,   //   ####
    0b10000,   //  #
    0b10000,   //  #
    0b01110,   //   ###
    0b00001,   //      #
    0b00001,   //      #
    0b11110    //  ####
};

// Exclamation mark: 2 cols x 7 rows (bit 1 = left col)
const uint8_t G_EXCL[7] = {
    0b11,      //  ##
    0b11,      //  ##
    0b11,      //  ##
    0b11,      //  ##
    0b00,      //
    0b11,      //  ##
    0b00       //
};

// Draw a glyph at (x,y). cols = number of bit columns in the mask.
void drawGlyph(int x, int y, const uint8_t* g, int cols, uint16_t col) {
    const int PX_W = 5;
    const int PX_H = 8;
    for (int row = 0; row < 7; row++) {
        for (int c = 0; c < cols; c++) {
            if (g[row] & (1 << (cols - 1 - c))) {
                canvas.fillRect(
                    x + c * PX_W,
                    y + row * PX_H,
                    PX_W - 1,
                    PX_H - 1,
                    col
                );
            }
        }
    }
}

void renderSplash() {
    canvas.fillSprite(BLACK);

    // Double border for depth
    canvas.drawRect(0, 0, 240, 135, 0x07E0);          // outer green
    canvas.drawRect(2, 2, 236, 131, 0x03E0);           // inner darker green

    // Scanline effect: subtle dark horizontal stripes across the art area
    for (int y = 18; y < 88; y += 2) {
        canvas.drawFastHLine(4, y, 232, 0x0200);        // near-black green tint
    }

    // ── Pixel-art "NEWS!" ──────────────────────────
    //  PX_W=5, PX_H=8 -> letter = 25x56 px
    //  N,E,W,S: 5-col glyphs  ->  width = 5*5 = 25 px
    //  !       : 2-col glyph  ->  width = 2*5 = 10 px
    //  gap between letters    :  5 px
    //  total = (25+5)*4 + 10  = 130 px  -> start x = (240-130)/2 = 55
    // ───────────────────────────────────────────────

    const int LWIDTH = 25;   // letter pixel width
    const int GAP    = 5;    // gap between letters
    const int SX     = 55;   // start x
    const int SY     = 16;   // start y
    const uint16_t GC = 0x07E0;   // glyph color (green)

    drawGlyph(SX + 0*(LWIDTH+GAP), SY, G_N,    5, GC);
    drawGlyph(SX + 1*(LWIDTH+GAP), SY, G_E,    5, GC);
    drawGlyph(SX + 2*(LWIDTH+GAP), SY, G_W,    5, GC);
    drawGlyph(SX + 3*(LWIDTH+GAP), SY, G_S,    5, GC);
    drawGlyph(SX + 4*(LWIDTH+GAP), SY, G_EXCL, 2, GC);

    // ── Decorative line ────────────────────────────
    canvas.drawFastHLine(10, 90, 220, 0x07E0);
    canvas.drawFastHLine(10, 92, 220, 0x03E0);

    // ── Subtitle ───────────────────────────────────
    canvas.setTextFont(2);
    canvas.setTextColor(0x8410);   // medium grey
    canvas.setCursor(44, 98);
    canvas.print("M5CYBER  MULTI-FEED  v2");

    // ── Hint ───────────────────────────────────────
    canvas.setTextFont(1);
    canvas.setTextColor(0x2104);   // dark grey
    canvas.setCursor(72, 119);
    canvas.print("connecting to WiFi...");

    canvas.pushSprite(0, 0);
}

// =====================================================
//  FETCH RSS
// =====================================================

void fetchRSS() {
    appMode = MODE_LOADING;
    renderUI();

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    http.begin(client, FEEDS[currentFeed].url);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        totalNews = 0;
        int pos = payload.indexOf("<item>");
        while (pos != -1 && totalNews < MAX_NEWS) {
            int ts = payload.indexOf("<title>", pos);
            int te = payload.indexOf("</title>", ts);
            if (ts != -1 && te != -1) {
                String title = payload.substring(ts + 7, te);
                title = cleanText(title);
                if (title.length() > 0) newsTitles[totalNews++] = title;
            }
            pos = payload.indexOf("<item>", pos + 1);
        }
        wifiError = false;
    } else {
        wifiError = true;
        totalNews = 1;
        newsTitles[0] = "HTTP error " + String(httpCode);
    }
    http.end();

    currentNews = 0;
    wrapText();
    appMode = MODE_NEWS;
}

// =====================================================
//  WIFI PORTAL
// =====================================================

void startWifiPortal() {
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);

    canvas.fillSprite(BLACK);
    canvas.fillRect(0, 0, 240, 18, 0x4208);
    canvas.setTextColor(WHITE);
    canvas.setTextFont(1);
    canvas.setCursor(6, 5);
    canvas.print("WiFi SETUP");

    canvas.setTextFont(2);
    canvas.setTextColor(WHITE);
    canvas.setCursor(28, 38);
    canvas.print("Connect to:");
    canvas.setTextColor(0x07E0);
    canvas.setCursor(52, 60);
    canvas.print("M5-CYBER");

    canvas.setTextFont(1);
    canvas.setTextColor(DARKGREY);
    canvas.setCursor(14, 94);
    canvas.print("Portal closes in 3 minutes.");
    canvas.setCursor(22, 110);
    canvas.print("Timeout -> device restart");
    canvas.pushSprite(0, 0);

    if (!wm.autoConnect("M5-CYBER")) ESP.restart();
}

// =====================================================
//  RENDER: LOADING
// =====================================================

void renderLoading() {
    canvas.fillSprite(BLACK);

    uint16_t fc = FEEDS[currentFeed].color;
    canvas.fillRect(0, 0, 240, 18, fc);
    canvas.setTextColor(BLACK);
    canvas.setTextFont(1);
    canvas.setCursor(6, 5);
    canvas.print(FEEDS[currentFeed].name);

    canvas.setTextColor(WHITE);
    canvas.setTextFont(2);
    canvas.setCursor(54, 50);
    canvas.print("LOADING...");

    canvas.fillCircle(102, 88, 5, fc);
    canvas.drawCircle(120, 88, 5, fc);
    canvas.drawCircle(138, 88, 5, fc);

    canvas.pushSprite(0, 0);
}

// =====================================================
//  RENDER: MENU
// =====================================================

void renderMenu() {
    canvas.fillSprite(BLACK);

    // Header
    canvas.fillRect(0, 0, 240, 18, 0x630C);
    canvas.setTextColor(WHITE);
    canvas.setTextFont(1);
    canvas.setCursor(6, 5);
    canvas.print("SELECT FEED SOURCE");

    const int ROW_H    = 16;
    const int START_Y  = 20;
    const int VIS_ROWS = 6;

    int scrollOff = 0;
    if (menuCursor >= VIS_ROWS) scrollOff = menuCursor - VIS_ROWS + 1;

    for (int i = 0; i < VIS_ROWS && (i + scrollOff) < FEED_COUNT; i++) {
        int  idx = i + scrollOff;
        int  y   = START_Y + i * ROW_H;
        bool sel = (idx == menuCursor);

        if (sel) {
            canvas.fillRect(0, y, 230, ROW_H - 1, FEEDS[idx].color);
            canvas.setTextColor(BLACK);
        } else {
            canvas.setTextColor(0xC618);
        }
        canvas.setTextFont(2);
        canvas.setCursor(8, y + 1);
        canvas.print(FEEDS[idx].name);

        if (idx == currentFeed) {
            canvas.setTextFont(1);
            canvas.setTextColor(sel ? (uint16_t)BLACK : FEEDS[idx].color);
            canvas.setCursor(183, y + 4);
            canvas.print("[ON]");
        }
    }

    // Scrollbar
    if (FEED_COUNT > VIS_ROWS) {
        int trackH = VIS_ROWS * ROW_H;
        int thumbH = max(4, trackH * VIS_ROWS / FEED_COUNT);
        int thumbY = START_Y + (menuCursor * (trackH - thumbH)) / max(1, FEED_COUNT - 1);
        canvas.fillRect(234, START_Y, 4, trackH, 0x2104);
        canvas.fillRect(234, thumbY,  4, thumbH, 0xC618);
    }

    // Footer
    canvas.setTextColor(0x4208);
    canvas.setTextFont(1);
    canvas.setCursor(8, 122);
    canvas.print("B NEXT FEED     A SELECT     Ahold EXIT");

    canvas.pushSprite(0, 0);
}

// =====================================================
//  RENDER: NEWS
// =====================================================

void renderNews() {
    if (displaySleeping) return;
    canvas.fillSprite(BLACK);

    // Header
    uint16_t hdrColor = wifiError ? (uint16_t)RED : FEEDS[currentFeed].color;
    canvas.fillRect(0, 0, 240, 18, hdrColor);
    canvas.setTextColor(BLACK);
    canvas.setTextFont(1);
    char hdr[36];
    sprintf(hdr, "%s  %d/%d", FEEDS[currentFeed].name, currentNews + 1, totalNews);
    canvas.setCursor(6, 5);
    canvas.print(hdr);

    int  bat = StickCP2.Power.getBatteryLevel();
    char batStr[8];
    sprintf(batStr, "%d%%", bat);
    canvas.setTextColor(bat <= 20 ? (uint16_t)RED : (uint16_t)BLACK);
    canvas.setCursor(205, 5);
    canvas.print(batStr);

    // News box
    canvas.drawRect(0, 20, 240, 96, 0x4208);

    // Text
    canvas.setTextColor(WHITE);
    canvas.setTextFont(2);
    int y = 28;
    for (int i = currentLineOffset;
         i < min(currentLineOffset + VISIBLE_LINES, (int)wrappedLines.size());
         i++) {
        canvas.setCursor(8, y);
        canvas.print(wrappedLines[i]);
        y += 18;
    }

    // Scroll thumb
    if ((int)wrappedLines.size() > VISIBLE_LINES) {
        int total  = (int)wrappedLines.size();
        int trackH = 80;
        int thumbH = max(6, trackH * VISIBLE_LINES / total);
        int maxOff = total - VISIBLE_LINES;
        int thumbY = 22 + (currentLineOffset * (trackH - thumbH)) / max(1, maxOff);
        canvas.fillRect(234, 22, 4, trackH, 0x2104);
        canvas.fillRect(234, thumbY, 4, thumbH, FEEDS[currentFeed].color);
    }

    // Footer
    bool hasMore = (currentLineOffset + VISIBLE_LINES < (int)wrappedLines.size());
    canvas.setTextFont(1);
    if (hasMore) {
        canvas.setTextColor(FEEDS[currentFeed].color);
        canvas.setCursor(8, 122);
        canvas.print("A NEXT   B SCROLL    Ahold MENU");
    } else {
        canvas.setTextColor(0x4208);
        canvas.setCursor(8, 122);
        canvas.print("A NEXT   B TOP       Ahold MENU");
    }

    canvas.pushSprite(0, 0);
}

// =====================================================
//  MASTER RENDER
// =====================================================

void renderUI() {
    switch (appMode) {
        case MODE_LOADING: renderLoading(); break;
        case MODE_MENU:    renderMenu();    break;
        case MODE_NEWS:    renderNews();    break;
    }
}

// =====================================================
//  WAKE / DIM
// =====================================================

void wakeDisplay() {
    if (displaySleeping) {
        StickCP2.Display.wakeup();
        StickCP2.Display.setBrightness(BRIGHTNESS_FULL);
        displaySleeping = false;
        displayDimmed   = false;
    } else if (displayDimmed) {
        StickCP2.Display.setBrightness(BRIGHTNESS_FULL);
        displayDimmed = false;
    }
    lastInteraction = millis();
}

// =====================================================
//  SETUP
// =====================================================

void setup() {
    auto cfg = M5.config();
    StickCP2.begin(cfg);
    StickCP2.Display.setRotation(1);
    StickCP2.Display.setBrightness(BRIGHTNESS_FULL);
    canvas.createSprite(240, 135);

    renderSplash();
    delay(2000);

    startWifiPortal();
    fetchRSS();
    renderUI();

    lastRefresh     = millis();
    lastInteraction = millis();
}

// =====================================================
//  LOOP
// =====================================================

void loop() {
    StickCP2.update();

    unsigned long idleMs = millis() - lastInteraction;

    // Auto-dim
    if (!displaySleeping && !displayDimmed && idleMs > DIM_TIMEOUT) {
        StickCP2.Display.setBrightness(BRIGHTNESS_DIM);
        displayDimmed = true;
    }

    // Auto-sleep
    if (!displaySleeping && idleMs > SCREEN_TIMEOUT) {
        StickCP2.Display.sleep();
        displaySleeping = true;
        displayDimmed   = false;
    }

    // Wake from dim or sleep: first press wakes only, no action
    if (displaySleeping || displayDimmed) {
        if (StickCP2.BtnA.wasPressed() || StickCP2.BtnB.wasPressed()) {
            wakeDisplay();
        }
        delay(20);
        return;
    }

    // ────────────────────────────────────────────────
    //  MODE_MENU
    //    BtnB  short   -> cycle feed list (down, wraps)
    //    BtnA  short   -> SELECT highlighted feed + load
    //    BtnA  2s hold -> EXIT menu (no change)
    //
    //  NOTE: Button C (power) is not accessible with this
    //  library version. Update M5StickCPlus2 + M5Unified
    //  to latest to get StickCP2.BtnPWR support.
    // ────────────────────────────────────────────────
    if (appMode == MODE_MENU) {

        // BtnB -> scroll list down (wraps to top)
        if (StickCP2.BtnB.wasReleased()) {
            menuCursor = (menuCursor + 1) % FEED_COUNT;
            renderUI();
        }

        // BtnA short -> confirm selection
        if (StickCP2.BtnA.wasReleased()) {
            if (ignoreBtnA) {
                ignoreBtnA = false;
            } else {
                currentFeed = menuCursor;
                fetchRSS();
                renderUI();
            }
        }

        // BtnA hold -> exit without change
        if (StickCP2.BtnA.pressedFor(MENU_HOLD_MS) && !ignoreBtnA) {
            ignoreBtnA = true;
            appMode    = MODE_NEWS;
            renderUI();
        }

        lastInteraction = millis();
        delay(20);
        return;
    }

    // ────────────────────────────────────────────────
    //  MODE_NEWS
    // ────────────────────────────────────────────────

    // BtnA long -> open menu
    if (StickCP2.BtnA.pressedFor(MENU_HOLD_MS) && !ignoreBtnA) {
        ignoreBtnA = true;
        menuCursor = currentFeed;
        appMode    = MODE_MENU;
        renderUI();
        lastInteraction = millis();
    }

    // BtnA short -> next article
    if (StickCP2.BtnA.wasReleased()) {
        if (ignoreBtnA) {
            ignoreBtnA = false;
        } else {
            currentNews = (currentNews + 1) % max(1, totalNews);
            wrapText();
            renderUI();
            lastInteraction = millis();
        }
    }

    // BtnB short -> scroll / back to top
    if (StickCP2.BtnB.wasReleased()) {
        if ((int)wrappedLines.size() > VISIBLE_LINES) {
            currentLineOffset++;
            if (currentLineOffset > (int)wrappedLines.size() - VISIBLE_LINES) {
                currentLineOffset = 0;
            }
        }
        renderUI();
        lastInteraction = millis();
    }

    // BtnB long (4s) -> WiFi portal
    if (StickCP2.BtnB.pressedFor(WIFI_HOLD_MS)) {
        startWifiPortal();
        fetchRSS();
        renderUI();
        lastInteraction = millis();
    }

    // Auto-refresh
    if (millis() - lastRefresh > REFRESH_INTERVAL) {
        lastRefresh = millis();
        fetchRSS();
        renderUI();
    }

    delay(20);
}
