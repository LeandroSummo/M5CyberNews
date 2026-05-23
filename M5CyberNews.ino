/*
 * =====================================================
 *  M5CYBER NEWS v2.5
 *  Multi-feed Italian RSS Reader + Local Clock
 *  Target: M5StickC Plus2  (ESP32-PICO, 240x135 px)
 *
 *  Boot directly to clock. Press A to open menu.
 *  Menu: feed sources, CLOCK mode, POWER OFF.
 * =====================================================
 */

#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <time.h>
#include <vector>

// =====================================================
//  DISPLAY & CANVAS
// =====================================================

M5Canvas canvas(&StickCP2.Display);

// =====================================================
//  CONFIG - Display & Timing
// =====================================================

#define MAX_NEWS          15
#define CHARS_PER_LINE    22
#define VISIBLE_LINES     4

#define REFRESH_INTERVAL  60000UL
#define DIM_TIMEOUT       12000UL
#define SCREEN_TIMEOUT    20000UL
#define CLOCK_RETURN_MS   30000UL
#define BRIGHTNESS_FULL   200
#define BRIGHTNESS_DIM    40
#define MENU_HOLD_MS      2000UL
#define WIFI_HOLD_MS      4000UL

// =====================================================
//  CONFIG - Timezone
// =====================================================

#define NTP_SERVER1       "pool.ntp.org"
#define NTP_SERVER2       "time.nist.gov"

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

const int FEED_COUNT      = sizeof(FEEDS) / sizeof(FEEDS[0]);
const int CLOCK_IDX       = FEED_COUNT;
const int SET_TZ_IDX      = FEED_COUNT + 1;
const int POWER_OFF_IDX   = FEED_COUNT + 2;
const int MENU_ITEMS      = FEED_COUNT + 3;

// =====================================================
//  SOUND - Note Frequencies
// =====================================================

#define NOTE_C5   523
#define NOTE_E5   659
#define NOTE_G5   784
#define NOTE_B5   988
#define NOTE_E6   1319

// =====================================================
//  APP STATE
// =====================================================

enum AppMode { MODE_CLOCK, MODE_NEWS, MODE_MENU, MODE_LOADING };

AppMode appMode        = MODE_CLOCK;
int     currentFeed    = 0;
int     menuCursor     = 0;
bool    ignoreBtnA     = false;

String  newsTitles[MAX_NEWS];
std::vector<String> wrappedLines;

int     totalNews      = 0;
int     currentNews    = 0;
int     currentLineOffset = 0;

bool    displaySleeping = false;
bool    displayDimmed   = false;
bool    wifiError       = false;
bool    ntpSynced       = false;

String  geoLocation     = "World";  // city, country detected by FreeGeoIP

// =====================================================
//  PERSISTENT STORAGE
// =====================================================

Preferences prefs;
bool    tzConfigured    = false;    // true if timezone was set (auto or manual)

unsigned long lastRefresh        = 0;
unsigned long lastInteraction    = 0;
unsigned long enteredNewsMode    = 0;

// =====================================================
//  UTILITY - Text Processing
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
//  UTILITY - Word Wrap
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
                line += ' ';
                line += word;
            } else {
                wrappedLines.push_back(line);
                line = word;
            }
            word = "";
        } else {
            if (word.length() >= CHARS_PER_LINE) {
                if (line.length() > 0) wrappedLines.push_back(line);
                wrappedLines.push_back(word);
                word = "";
                line = "";
            }
            word += c;
        }
    }
    if (line.length() > 0) wrappedLines.push_back(line);
    currentLineOffset = 0;
}

// =====================================================
//  APP STATE - Timezone display
// =====================================================

String  tzDisplay       = "UTC";        // Display string like "UTC+2"
int     tzOffsetHours   = 0;            // Numeric offset in hours

// =====================================================
//  PERSISTENT STORAGE - Load & Save Settings
// =====================================================

void loadSettings() {
    prefs.begin("m5cyber", true);  // read-only mode
    tzConfigured  = prefs.getBool("tzset", false);
    tzOffsetHours = prefs.getInt("tzoffset", 0);
    geoLocation   = prefs.getString("geoloc", "World");
    currentFeed   = prefs.getInt("feed", 0);
    prefs.end();
    
    // Rebuild display string from saved offset
    if (tzOffsetHours >= 0) {
        tzDisplay = "UTC+" + String(tzOffsetHours);
    } else {
        tzDisplay = "UTC" + String(tzOffsetHours);
    }
    
    // Clamp feed index in case of corruption
    if (currentFeed < 0 || currentFeed >= FEED_COUNT) currentFeed = 0;
}

void saveSettings() {
    prefs.begin("m5cyber", false);  // read-write mode
    prefs.putBool("tzset", tzConfigured);
    prefs.putInt("tzoffset", tzOffsetHours);
    prefs.putString("geoloc", geoLocation);
    prefs.putInt("feed", currentFeed);
    prefs.end();
}

// =====================================================
//  NETWORK - Geo Locate (get country, city, UTC offset)
// =====================================================

void geoLocate() {
    // Try multiple geo services with fallback chain
    // Primary: ipapi.co (reliable HTTP)
    // Secondary: freegeoip.app
    // Fallback: default UTC
    
    bool success = false;
    
    // ──── Try ipapi.co first ────────────────────────
    {
        WiFiClient client;
        HTTPClient http;
        http.setTimeout(5000);
        
        if (http.begin(client, "http://ipapi.co/json/")) {
            int httpCode = http.GET();
            
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                
                // Extract country_name
                int countryIdx = payload.indexOf("\"country_name\":");
                if (countryIdx != -1) {
                    int start = payload.indexOf("\"", countryIdx + 15) + 1;
                    int end = payload.indexOf("\"", start);
                    if (start > 0 && end > start) {
                        geoLocation = payload.substring(start, end);
                    }
                }
                
                // Extract city if available
                int cityIdx = payload.indexOf("\"city\":");
                if (cityIdx != -1) {
                    int start = payload.indexOf("\"", cityIdx + 7) + 1;
                    int end = payload.indexOf("\"", start);
                    if (start > 0 && end > start) {
                        String city = payload.substring(start, end);
                        if (city.length() > 0 && city != "Unknown") {
                            geoLocation = city + ", " + geoLocation;
                        }
                    }
                }
                
                // Extract utc_offset
                int offsetIdx = payload.indexOf("\"utc_offset\":");
                if (offsetIdx != -1) {
                    int start = payload.indexOf("\"", offsetIdx + 13) + 1;
                    int end = payload.indexOf("\"", start);
                    if (start > 0 && end > start) {
                        String offsetStr = payload.substring(start, end);
                        int colonIdx = offsetStr.indexOf(":");
                        if (colonIdx > 0) {
                            String hourPart = offsetStr.substring(0, colonIdx);
                            tzOffsetHours = hourPart.toInt();
                            
                            if (tzOffsetHours >= 0) {
                                tzDisplay = "UTC+" + String(tzOffsetHours);
                            } else {
                                tzDisplay = "UTC" + String(tzOffsetHours);
                            }
                            
                            configTime(tzOffsetHours * 3600, 0, NTP_SERVER1, NTP_SERVER2);
                            ntpSynced = true;
                            success = true;
                        }
                    }
                }
            }
            http.end();
        }
    }
    
    if (success) {
        tzConfigured = true;
        saveSettings();
        return;
    }
    
    // ──── Fallback: try FreeGeoIP ───────────────────
    {
        WiFiClient client;
        HTTPClient http;
        http.setTimeout(5000);
        
        if (http.begin(client, "http://freegeoip.app/json/")) {
            int httpCode = http.GET();
            
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                
                // Extract country_name
                int countryIdx = payload.indexOf("\"country_name\":");
                if (countryIdx != -1) {
                    int start = payload.indexOf("\"", countryIdx + 15) + 1;
                    int end = payload.indexOf("\"", start);
                    if (start > 0 && end > start) {
                        geoLocation = payload.substring(start, end);
                    }
                }
                
                // Extract city if available
                int cityIdx = payload.indexOf("\"city\":");
                if (cityIdx != -1) {
                    int start = payload.indexOf("\"", cityIdx + 7) + 1;
                    int end = payload.indexOf("\"", start);
                    if (start > 0 && end > start) {
                        String city = payload.substring(start, end);
                        if (city.length() > 0 && city != "Unknown") {
                            geoLocation = city + ", " + geoLocation;
                        }
                    }
                }
                
                // Extract utc_offset
                int offsetIdx = payload.indexOf("\"utc_offset\":");
                if (offsetIdx != -1) {
                    int start = payload.indexOf("\"", offsetIdx + 13) + 1;
                    int end = payload.indexOf("\"", start);
                    if (start > 0 && end > start) {
                        String offsetStr = payload.substring(start, end);
                        int colonIdx = offsetStr.indexOf(":");
                        if (colonIdx > 0) {
                            String hourPart = offsetStr.substring(0, colonIdx);
                            tzOffsetHours = hourPart.toInt();
                            
                            if (tzOffsetHours >= 0) {
                                tzDisplay = "UTC+" + String(tzOffsetHours);
                            } else {
                                tzDisplay = "UTC" + String(tzOffsetHours);
                            }
                            
                            configTime(tzOffsetHours * 3600, 0, NTP_SERVER1, NTP_SERVER2);
                            ntpSynced = true;
                            success = true;
                        }
                    }
                }
            }
            http.end();
        }
    }
    
    if (success) {
        tzConfigured = true;
        saveSettings();
        return;
    }
    
    // ──── Fallback: invite user to set timezone manually ────
    geoLocation = "SET TZ";
    tzDisplay = "UTC";
    tzOffsetHours = 0;
    configTime(0, 0, NTP_SERVER1, NTP_SERVER2);
}

// =====================================================
//  SOUND - Jingles
// =====================================================

void playStartupJingle() {
    StickCP2.Speaker.setVolume(160);
    StickCP2.Speaker.tone(NOTE_C5, 90);   delay(100);
    StickCP2.Speaker.tone(NOTE_E5, 90);   delay(100);
    StickCP2.Speaker.tone(NOTE_G5, 90);   delay(100);
    StickCP2.Speaker.tone(NOTE_B5, 90);   delay(100);
    StickCP2.Speaker.tone(NOTE_E6, 350);  delay(400);
    StickCP2.Speaker.stop();
}

void playShutdownJingle() {
    StickCP2.Speaker.setVolume(140);
    StickCP2.Speaker.tone(NOTE_E6, 120);  delay(140);
    StickCP2.Speaker.tone(NOTE_B5, 120);  delay(140);
    StickCP2.Speaker.tone(NOTE_G5, 120);  delay(140);
    StickCP2.Speaker.tone(NOTE_E5, 120);  delay(140);
    StickCP2.Speaker.tone(NOTE_C5, 500);  delay(560);
    StickCP2.Speaker.stop();
}

// =====================================================
//  RENDER - Clock (idle screen)
// =====================================================

void renderClock() {
    canvas.fillSprite(BLACK);

    // Header: location + UTC offset + battery
    canvas.fillRect(0, 0, 240, 18, 0x07E0);
    canvas.setTextFont(1);
    canvas.setTextColor(BLACK);
    canvas.setCursor(6, 4);
    
    String locDisplay = geoLocation;
    if (locDisplay.length() > 12) {
        locDisplay = locDisplay.substring(0, 12);
    }
    canvas.print(locDisplay.c_str());
    
    // Timezone in center of header
    int tzW = canvas.textWidth(tzDisplay.c_str());
    canvas.setCursor((240 - tzW) / 2, 4);
    canvas.print(tzDisplay.c_str());

    // Battery on right
    int bat = StickCP2.Power.getBatteryLevel();
    char batStr[8];
    sprintf(batStr, "%d%%", bat);
    canvas.setTextColor(bat <= 20 ? (uint16_t)RED : (uint16_t)BLACK);
    canvas.setCursor(200, 4);
    canvas.print(batStr);

    // Get time
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char timeStr[16];
    char dateStr[32];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);
    strftime(dateStr, sizeof(dateStr), "%a, %d %b %Y", timeinfo);

    // Big time at y=35
    canvas.setTextFont(7);
    canvas.setTextColor(0x07E0);
    int timeW = canvas.textWidth(timeStr);
    canvas.setCursor((240 - timeW) / 2, 35);
    canvas.print(timeStr);

    // Date at y=88
    canvas.setTextFont(2);
    canvas.setTextColor(0xC618);
    int dateW = canvas.textWidth(dateStr);
    canvas.setCursor((240 - dateW) / 2, 88);
    canvas.print(dateStr);

    // Footer
    canvas.setTextFont(1);
    canvas.setTextColor(0x2104);
    canvas.setCursor(40, 112);
    canvas.print("A to open menu");

    canvas.pushSprite(0, 0);
}

// =====================================================
//  RENDER - Loading
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
//  SET TIMEZONE - Structured, clear menu
// =====================================================

void setTimezoneMenu() {
    int tzMenuCursor = tzOffsetHours + 12;
    
    while (true) {
        StickCP2.update();
        
        canvas.fillSprite(BLACK);
        
        // ── Header ──────────────────────────────────
        canvas.fillRect(0, 0, 240, 18, 0xFD20);
        canvas.setTextColor(BLACK);
        canvas.setTextFont(1);
        canvas.setCursor(70, 5);
        canvas.print("SET TIMEZONE");
        
        // ── UTC value box (centered, boxed) ─────────
        int currentTZ = tzMenuCursor - 12;
        char tzStr[12];
        sprintf(tzStr, "UTC%+d", currentTZ);  // e.g. "UTC+2", "UTC-5", "UTC+0"
        
        // Box with border
        canvas.drawRoundRect(40, 24, 160, 42, 6, 0xFD20);
        canvas.drawRoundRect(41, 25, 158, 40, 6, 0xFD20);
        
        canvas.setTextFont(6);
        canvas.setTextColor(WHITE);
        int tzW = canvas.textWidth(tzStr);
        canvas.setCursor((240 - tzW) / 2, 30);
        canvas.print(tzStr);
        
        // ── Button A: UP ────────────────────────────
        int rowY = 78;
        // Button box
        canvas.fillRoundRect(14, rowY, 24, 16, 3, 0xFD20);
        canvas.setTextFont(1);
        canvas.setTextColor(BLACK);
        canvas.setCursor(23, rowY + 4);
        canvas.print("A");
        // Up arrow
        canvas.fillTriangle(48, rowY + 12, 54, rowY + 3, 60, rowY + 12, 0x07E0);
        // Label
        canvas.setTextColor(WHITE);
        canvas.setCursor(70, rowY + 4);
        canvas.print("UTC forward");
        
        // ── Button B: DOWN ──────────────────────────
        rowY = 98;
        canvas.fillRoundRect(14, rowY, 24, 16, 3, 0xFD20);
        canvas.setTextColor(BLACK);
        canvas.setCursor(23, rowY + 4);
        canvas.print("B");
        // Down arrow
        canvas.fillTriangle(48, rowY + 3, 54, rowY + 12, 60, rowY + 3, 0xF800);
        // Label
        canvas.setTextColor(WHITE);
        canvas.setCursor(70, rowY + 4);
        canvas.print("UTC backward");
        
        // ── Confirm hint ────────────────────────────
        rowY = 118;
        canvas.setTextColor(0xC618);
        canvas.setCursor(14, rowY);
        canvas.print("Hold");
        canvas.fillRoundRect(48, rowY - 2, 20, 13, 3, 0x07E0);
        canvas.setTextColor(BLACK);
        canvas.setCursor(55, rowY);
        canvas.print("B");
        canvas.setTextColor(0xC618);
        canvas.setCursor(74, rowY);
        canvas.print("to SAVE");
        
        canvas.pushSprite(0, 0);
        
        // ── Controls ────────────────────────────────
        if (StickCP2.BtnA.wasReleased()) {
            tzMenuCursor = (tzMenuCursor + 1) % 25;
        }
        
        if (StickCP2.BtnB.wasReleased()) {
            tzMenuCursor = (tzMenuCursor - 1 + 25) % 25;
        }
        
        if (StickCP2.BtnB.pressedFor(1500)) {
            tzOffsetHours = tzMenuCursor - 12;
            
            if (tzOffsetHours >= 0) {
                tzDisplay = "UTC+" + String(tzOffsetHours);
            } else {
                tzDisplay = "UTC" + String(tzOffsetHours);
            }
            
            geoLocation = tzDisplay;  // header no longer shows "SET TZ"
            
            configTime(tzOffsetHours * 3600, 0, NTP_SERVER1, NTP_SERVER2);
            ntpSynced = true;
            
            // Save to persistent storage
            tzConfigured = true;
            saveSettings();
            
            delay(500);
            return;
        }
        
        delay(20);
    }
}

// =====================================================
//  RENDER - Menu
// =====================================================

void renderMenu() {
    canvas.fillSprite(BLACK);
    canvas.fillRect(0, 0, 240, 18, 0x630C);
    canvas.setTextColor(WHITE);
    canvas.setTextFont(1);
    canvas.setCursor(6, 5);
    canvas.print("SELECT SOURCE");

    const int ROW_H    = 16;
    const int START_Y  = 20;
    const int VIS_ROWS = 6;

    int scrollOff = 0;
    if (menuCursor >= VIS_ROWS) scrollOff = menuCursor - VIS_ROWS + 1;

    for (int i = 0; i < VIS_ROWS && (i + scrollOff) < MENU_ITEMS; i++) {
        int idx = i + scrollOff;
        int y   = START_Y + i * ROW_H;
        bool sel = (idx == menuCursor);

        if (idx == CLOCK_IDX) {
            if (sel) {
                canvas.fillRect(0, y, 230, ROW_H - 1, 0x07E0);
                canvas.setTextColor(BLACK);
            } else {
                canvas.setTextColor(0x07E0);
            }
            canvas.setTextFont(2);
            canvas.setCursor(8, y + 1);
            canvas.print("CLOCK");

        } else if (idx == SET_TZ_IDX) {
            if (sel) {
                canvas.fillRect(0, y, 230, ROW_H - 1, 0xFD20);
                canvas.setTextColor(BLACK);
            } else {
                canvas.setTextColor(0xFD20);
            }
            canvas.setTextFont(2);
            canvas.setCursor(8, y + 1);
            canvas.print("SET TZ");

        } else if (idx == POWER_OFF_IDX) {
            if (sel) {
                canvas.fillRect(0, y, 230, ROW_H - 1, RED);
                canvas.setTextColor(BLACK);
            } else {
                canvas.setTextColor(RED);
            }
            canvas.setTextFont(2);
            canvas.setCursor(8, y + 1);
            canvas.print("POWER OFF");

        } else {
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
    }

    if (MENU_ITEMS > VIS_ROWS) {
        int trackH = VIS_ROWS * ROW_H;
        int thumbH = max(4, trackH * VIS_ROWS / MENU_ITEMS);
        int thumbY = START_Y + (menuCursor * (trackH - thumbH)) / max(1, MENU_ITEMS - 1);
        canvas.fillRect(234, START_Y, 4, trackH, 0x2104);
        canvas.fillRect(234, thumbY, 4, thumbH, 0xC618);
    }

    canvas.setTextColor(0x4208);
    canvas.setTextFont(1);
    canvas.setCursor(8, 122);
    canvas.print("B NEXT   A SELECT   Ahold EXIT");

    canvas.pushSprite(0, 0);
}

// =====================================================
//  RENDER - News
// =====================================================

void renderNews() {
    if (displaySleeping) return;
    canvas.fillSprite(BLACK);

    uint16_t hdrColor = wifiError ? (uint16_t)RED : FEEDS[currentFeed].color;
    canvas.fillRect(0, 0, 240, 18, hdrColor);
    canvas.setTextColor(BLACK);
    canvas.setTextFont(1);
    char hdr[36];
    sprintf(hdr, "%s  %d/%d", FEEDS[currentFeed].name, currentNews + 1, totalNews);
    canvas.setCursor(6, 5);
    canvas.print(hdr);

    int bat = StickCP2.Power.getBatteryLevel();
    char batStr[8];
    sprintf(batStr, "%d%%", bat);
    canvas.setTextColor(bat <= 20 ? (uint16_t)RED : (uint16_t)BLACK);
    canvas.setCursor(205, 5);
    canvas.print(batStr);

    canvas.drawRect(0, 20, 240, 96, 0x4208);

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

    if ((int)wrappedLines.size() > VISIBLE_LINES) {
        int total  = (int)wrappedLines.size();
        int trackH = 80;
        int thumbH = max(6, trackH * VISIBLE_LINES / total);
        int maxOff = total - VISIBLE_LINES;
        int thumbY = 22 + (currentLineOffset * (trackH - thumbH)) / max(1, maxOff);
        canvas.fillRect(234, 22, 4, trackH, 0x2104);
        canvas.fillRect(234, thumbY, 4, thumbH, FEEDS[currentFeed].color);
    }

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
//  RENDER - Bye Bye
// =====================================================

void renderByeBye() {
    canvas.fillSprite(BLACK);
    canvas.drawRect(0, 0, 240, 135, RED);
    canvas.drawRect(2, 2, 236, 131, 0x8000);
    canvas.setTextFont(4);
    canvas.setTextColor(RED);
    canvas.setCursor(44, 26);
    canvas.print("BYE BYE!");
    canvas.drawFastHLine(20, 70, 200, 0x8000);
    canvas.setTextFont(2);
    canvas.setTextColor(DARKGREY);
    canvas.setCursor(44, 80);
    canvas.print("powering off...");
    canvas.setTextFont(1);
    canvas.setTextColor(0x4208);
    canvas.setCursor(20, 112);
    canvas.print("hold side button 2s to power on");
    canvas.pushSprite(0, 0);
}

// =====================================================
//  RENDER - Master Dispatch
// =====================================================

void renderUI() {
    switch (appMode) {
        case MODE_CLOCK:   renderClock();   break;
        case MODE_LOADING: renderLoading(); break;
        case MODE_MENU:    renderMenu();    break;
        case MODE_NEWS:    renderNews();    break;
    }
}

// =====================================================
//  POWER - Off
// =====================================================

void doPowerOff() {
    renderByeBye();
    playShutdownJingle();
    delay(600);
    StickCP2.Power.powerOff();
}

// =====================================================
//  DISPLAY - Wake & Dim
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
//  NETWORK - Fetch RSS
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
    enteredNewsMode = millis();
}

// =====================================================
//  NETWORK - WiFi Setup
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
    
    // ── Timezone handling ───────────────────────────
    if (tzConfigured) {
        // Already configured (loaded from storage) - skip slow geoloc
        // Just apply the saved offset directly
        configTime(tzOffsetHours * 3600, 0, NTP_SERVER1, NTP_SERVER2);
    } else {
        // First time - detect location and timezone
        geoLocate();
    }
    
    // Wait for NTP sync (max 5 seconds)
    for (int i = 0; i < 50; i++) {
        delay(100);
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        if (timeinfo->tm_year > 120) {
            ntpSynced = true;
            break;
        }
    }
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

    StickCP2.Speaker.begin();
    StickCP2.Speaker.setVolume(160);

    // Load saved settings (timezone, location, last feed)
    loadSettings();

    renderClock();
    playStartupJingle();

    startWifiPortal();
    
    lastRefresh     = millis();
    lastInteraction = millis();
    enteredNewsMode = 0;

    appMode = MODE_CLOCK;
    renderUI();
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

    // Wake
    if (displaySleeping || displayDimmed) {
        if (StickCP2.BtnA.wasPressed() || StickCP2.BtnB.wasPressed()) {
            wakeDisplay();
        }
        delay(20);
        return;
    }

    // ─────────────────────────────────────────────
    //  MODE_CLOCK
    // ─────────────────────────────────────────────
    if (appMode == MODE_CLOCK) {
        static unsigned long lastClockRefresh = 0;
        if (millis() - lastClockRefresh > 1000) {
            lastClockRefresh = millis();
            renderUI();
        }

        if (StickCP2.BtnA.wasReleased()) {
            menuCursor = currentFeed;
            appMode    = MODE_MENU;
            renderUI();
            lastInteraction = millis();
        }

        lastInteraction = millis();
        delay(20);
        return;
    }

    // ─────────────────────────────────────────────
    //  MODE_NEWS
    // ─────────────────────────────────────────────
    if (appMode == MODE_NEWS) {
        if (enteredNewsMode > 0 && millis() - enteredNewsMode > CLOCK_RETURN_MS) {
            appMode = MODE_CLOCK;
            renderUI();
            enteredNewsMode = 0;
            lastInteraction = millis();
        }

        if (StickCP2.BtnA.pressedFor(MENU_HOLD_MS) && !ignoreBtnA) {
            ignoreBtnA = true;
            menuCursor = currentFeed;
            appMode    = MODE_MENU;
            renderUI();
            lastInteraction = millis();
        }

        if (StickCP2.BtnA.wasReleased()) {
            if (ignoreBtnA) {
                ignoreBtnA = false;
            } else {
                currentNews = (currentNews + 1) % max(1, totalNews);
                wrapText();
                renderUI();
                lastInteraction = millis();
                enteredNewsMode = millis();
            }
        }

        if (StickCP2.BtnB.wasReleased()) {
            if ((int)wrappedLines.size() > VISIBLE_LINES) {
                currentLineOffset++;
                if (currentLineOffset > (int)wrappedLines.size() - VISIBLE_LINES) {
                    currentLineOffset = 0;
                }
            }
            renderUI();
            lastInteraction = millis();
            enteredNewsMode = millis();
        }

        if (StickCP2.BtnB.pressedFor(WIFI_HOLD_MS)) {
            startWifiPortal();
            fetchRSS();
            renderUI();
            lastInteraction = millis();
            enteredNewsMode = millis();
        }

        if (millis() - lastRefresh > REFRESH_INTERVAL) {
            lastRefresh = millis();
            fetchRSS();
            renderUI();
        }

        delay(20);
        return;
    }

    // ─────────────────────────────────────────────
    //  MODE_MENU
    // ─────────────────────────────────────────────
    if (appMode == MODE_MENU) {
        if (StickCP2.BtnB.wasReleased()) {
            menuCursor = (menuCursor + 1) % MENU_ITEMS;
            renderUI();
        }

        if (StickCP2.BtnA.wasReleased()) {
            if (ignoreBtnA) {
                ignoreBtnA = false;
            } else if (menuCursor == CLOCK_IDX) {
                appMode = MODE_CLOCK;
                renderUI();
                lastInteraction = millis();
            } else if (menuCursor == SET_TZ_IDX) {
                setTimezoneMenu();
                renderUI();
                lastInteraction = millis();
            } else if (menuCursor == POWER_OFF_IDX) {
                doPowerOff();
            } else {
                currentFeed = menuCursor;
                saveSettings();
                fetchRSS();
                renderUI();
                enteredNewsMode = millis();
            }
        }

        if (StickCP2.BtnA.pressedFor(MENU_HOLD_MS) && !ignoreBtnA) {
            ignoreBtnA = true;
            appMode    = MODE_NEWS;
            renderUI();
            enteredNewsMode = millis();
        }

        lastInteraction = millis();
        delay(20);
        return;
    }

    delay(20);
}