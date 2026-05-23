# M5CYBER NEWS

![M5Cyber News - splash screen](docs/hero.jpg)

**M5CYBER NEWS** is a standalone Italian RSS news reader that runs on the
[M5StickC Plus2](https://docs.m5stack.com/en/core/M5StickC%20PLUS2) — a
thumb-sized ESP32 device with a 1.14" colour display.

No phone. No app. No subscription. Just press a button and read the headlines.

The device connects to your WiFi, fetches fresh headlines from seven major
Italian outlets every 60 seconds, and displays them word-wrapped on screen.
A two-button interface lets you flip through articles, scroll long titles, and
switch between news sources on the fly — all without touching a computer.

It is designed to sit on a desk, in a workshop, or in a pocket: something
glanceable and distraction-free for staying informed during the day.

![status](https://img.shields.io/badge/platform-M5StickC%20Plus2-orange)
![status](https://img.shields.io/badge/language-C%2B%2B%20(Arduino)-blue)
![status](https://img.shields.io/badge/license-MIT-green)

---

## Screenshots

| Splash screen | Reading news | Feed menu |
|:---:|:---:|:---:|
| ![Splash](docs/screen_splash.jpg) | ![News](docs/screen_news.jpg) | ![Menu](docs/screen_menu.jpg) |
| Pixel-art boot screen | Headlines word-wrapped, scrollbar on the right, source and battery in the header | Switch between 7 sources with two buttons |

---

## Features

- **7 Italian news feeds** — ANSA, Corriere della Sera, Repubblica, Il Sole 24 Ore,
  TGcom24, La Stampa, Il Fatto Quotidiano.
- **On-device feed menu** — switch source without reflashing.
- **Pixel-art splash screen** — bitmap "NEWS!" drawn at boot from a custom 5×7
  pixel font rendered with `fillRect()`.
- **Per-source colour coding** — the header colour tells you which outlet you
  are reading at a glance.
- **Word-wrapped headlines** with a proportional vertical scrollbar for long
  titles.
- **Auto-refresh** every 60 seconds in the background.
- **Battery saver** — display dims to 16% brightness after 12 s of inactivity,
  sleeps after 20 s; first button press wakes it without triggering an action.
- **WiFi captive portal** — first-run setup via WiFiManager; credentials are
  saved to flash and survive reboots. No hardcoded passwords.
- **Robust RSS text cleaning** — HTML entities, UTF-8 accented vowels,
  typographic quotes, Windows-1252 leftovers, and CDATA wrappers are all
  stripped before display.

---

## Hardware

| Component | Notes |
|-----------|-------|
| **M5StickC Plus2** | ESP32-PICO, 240×135 px ST7789V2 display, 200 mAh battery |
| USB-C cable | For flashing only |
| 2.4 GHz WiFi | For fetching feeds |

---

## Feed sources

| # | Outlet              | RSS URL                                                  |
|---|---------------------|----------------------------------------------------------|
| 1 | ANSA                | `https://www.ansa.it/sito/ansait_rss.xml`                |
| 2 | Corriere della Sera | `https://xml2.corriereobjects.it/rss/homepage.xml`       |
| 3 | Repubblica          | `https://www.repubblica.it/rss/homepage/rss2.0.xml`      |
| 4 | Il Sole 24 Ore      | `https://www.ilsole24ore.com/rss/italia.xml`             |
| 5 | TGcom24             | `https://www.tgcom24.mediaset.it/rss/homepage.xml`       |
| 6 | La Stampa           | `https://www.lastampa.it/rss/copertina.xml`              |
| 7 | Il Fatto Quotidiano | `https://www.ilfattoquotidiano.it/feed/`                 |

RSS endpoints occasionally change. If a feed stops loading, update its URL
inside the `FEEDS[]` array — see [Customization](#customization).

---

## Controls

### Reading news

| Button | Action |
|--------|--------|
| **A** short press | Next article |
| **B** short press | Scroll text down / back to top |
| **A** hold 2 s | Open feed menu |
| **B** hold 4 s | Open WiFi configuration portal |

### Feed menu

| Button | Action |
|--------|--------|
| **B** short press | Cycle to next feed (wraps around) |
| **A** short press | Select feed and load |
| **A** hold 2 s | Exit menu without changing source |

> **Button C (power button):** the current M5StickCPlus2 library does not
> expose `BtnPWR` on older releases. Update **M5StickCPlus2**, **M5Unified**,
> and **M5GFX** to the latest versions via the Library Manager to gain
> B=up / C=down / A=select three-button navigation.

---

## Installation

### 1. Install the ESP32 board package

In Arduino IDE: **Tools → Board → Boards Manager** → search
**esp32 by Espressif Systems** → Install.

Select **M5StickC Plus2** as the target board.

### 2. Install required libraries

**Tools → Manage Libraries**, install the latest version of each:

| Library | Author |
|---------|--------|
| M5StickCPlus2 | M5Stack |
| M5Unified | M5Stack |
| M5GFX | M5Stack |
| WiFiManager | tzapu |

### 3. Upload

Open `M5CyberNews.ino`, select the correct serial port, click **Upload**.

---

## First run — WiFi setup

The device has no hardcoded network credentials. On first boot it starts a
captive portal:

1. On any phone or laptop, connect to the WiFi network **`M5-CYBER`**.
2. A configuration page opens automatically in your browser.
3. Choose your home network, enter the password, save.
4. The device connects and starts loading headlines.

To reconfigure WiFi at any time: hold **Button B for 4 seconds** in reading
mode. The portal reopens and closes automatically after 3 minutes if unused.

---

## Customization

All user-facing settings are `#define` constants and the `FEEDS[]` array at
the top of `M5CyberNews.ino`.

### Add or replace a feed

```cpp
const FeedSource FEEDS[] = {
    { "ANSA",   "https://www.ansa.it/sito/ansait_rss.xml",  0x07E0 },
    { "MY FEED","https://example.com/feed.xml",             0xF800 },
    // up to however many you need
};
```

Each entry is `{ display name (max ~9 chars), URL, RGB565 colour }`.
The colour becomes the header bar and accent colour for that source.

### Timing and power

| Constant | Default | Description |
|----------|---------|-------------|
| `REFRESH_INTERVAL` | `60000` ms | How often to fetch fresh headlines |
| `DIM_TIMEOUT` | `12000` ms | Idle time before screen dims |
| `SCREEN_TIMEOUT` | `20000` ms | Idle time before screen sleeps |
| `BRIGHTNESS_FULL` | `200` | Normal backlight (0–255) |
| `BRIGHTNESS_DIM` | `40` | Dimmed backlight (0–255) |
| `MAX_NEWS` | `15` | Maximum headlines stored per feed |

---

## How it works

```
boot
 └─ renderSplash()          draws pixel-art "NEWS!" from 5×7 bitmaps
 └─ startWifiPortal()       connects or opens captive portal
 └─ fetchRSS()              HTTPS GET → parse <title> tags → cleanText()
 └─ main loop
      ├─ auto-dim / sleep    power management on idle
      ├─ BtnA / BtnB         state machine: MODE_NEWS / MODE_MENU
      └─ auto-refresh        re-fetches every REFRESH_INTERVAL ms
```

Headlines are fetched over HTTPS (`WiFiClientSecure`, certificate check
disabled for simplicity). The raw XML is scanned with `indexOf()` — no XML
library dependency. Text is cleaned, word-wrapped into a `std::vector<String>`,
and rendered to an off-screen `M5Canvas` sprite, then pushed to the display in
a single `pushSprite()` call to avoid flicker.

---

## Project structure

```
M5CyberNews/
├── M5CyberNews.ino   Main firmware (single-file Arduino sketch)
├── README.md
├── LICENSE
├── .gitignore
└── docs/             Device photos used in this README
```

---

## License

Released under the **MIT License** — see [LICENSE](LICENSE).

News content belongs to the respective publishers. This project only fetches
and displays their publicly available RSS headlines; no content is stored or
redistributed.
