# Copilot Meter for the ESP32-2432S028 (CYD)

Shows your monthly **GitHub Copilot AI credit** usage as `used / allowance` on the ESP32-2432S028 "Cheap Yellow Display", next to an animated pixel pilot whose mood follows how much of the month's allowance you have burned. Built on a minimal CYD base derived from [NerdMiner_v2](https://github.com/BitMaker-hub/NerdMiner_v2).

## What's on screen

- Credits used this month (big number), `of <allowance>`, and the percentage
- Progress bar in the mood colour, with a white tick showing where you'd be if usage were spread evenly over the month
- Pace (`On track`, `Burning fast +12%`, `Under pace -8%`) and a projected month-end total (red if it would exceed the allowance)
- Time until the allowance resets (00:00 UTC on the 1st)
- Status bar: WiFi signal, `Updated HH:MM`, and the device IP or any error

| % of allowance used | Mood | RGB LED |
| --- | --- | --- |
| 0 – 25 | Happy: ^ ^ eyes, blush, sparkles | green |
| 25 – 50 | Cruising: relaxed, blinking | green |
| 50 – 75 | Focused: determined brows, a sweat drop | yellow |
| 75 – 90 | Worried: nervous glances, wavy mouth, more sweat | yellow |
| 90 – 100 | Panic: shaking, flashing red goggles, `! !` | red |
| 100+ | Burnt out: smoke pouring out of X eyes, cracked goggles | blinking red |

## Requirements

- A Copilot plan billed to **your personal account** (Free, Pro, Pro+, Max). The user-level billing API does not report seats managed by an organization or enterprise.
- A **fine-grained personal access token** with *Account permissions → Plan → Read-only* and nothing else. Create one at <https://github.com/settings/personal-access-tokens/new>.

Usage comes from [`GET /users/{username}/settings/billing/ai_credit/usage`](https://docs.github.com/en/rest/billing/usage) (API version `2026-03-10`). The meter adds up `grossQuantity` across all usage items, which is every AI credit consumed this month, including credits covered by your allowance. The API doesn't return your allowance, so it's a setting. It defaults to 20,000 (Copilot Max); Pro is 1,500 and Pro+ is 7,000.

## Build and flash

```bash
pio run -e ESP32-2432S028R -t upload
pio device monitor
```

Use `-e ESP32-2432S028-2USB` for the revision with both micro-USB and USB-C (colours look inverted otherwise).

## First-time setup

1. Power the board. With no saved WiFi it starts a setup hotspot `CopilotMeter-XXXX`. The screen shows the hotspot's password (stays the same for each device) and a QR code that joins it.
2. Your phone's captive portal opens. If it doesn't, browse to `192.168.4.1` and choose **Configure WiFi**.
3. Choose your 2.4 GHz network and enter its password. Fill in your GitHub username and token, then **Save**.
4. The device joins your WiFi and shows the dashboard within a few seconds.

If the WiFi is already saved but you haven't set a token yet, the screen shows **Almost there** with a QR code and link to `http://<device-ip>/param`. If that page doesn't load (common on guest networks), tap the screen to open the setup hotspot instead.

## Changing settings later

- **Same network:** open `http://<device-ip>/param` (the IP is shown in the status bar) or `http://copilot-meter.local/param` and choose **Setup**. Leave the token field blank to keep the saved token; the page never shows it.
- **Guest networks / client isolation, or a new WiFi:** hold **BOOT for 3 s** to open the setup hotspot. To change only the settings, leave the SSID field empty; the device reconnects to the saved network after you save. The hotspot closes after 5 minutes without a connected client.
- **USB serial:** see the console commands below.

| Setting | Default |
| --- | --- |
| Monthly AI credit allowance | 20000 |
| Refresh interval | 30 minutes (5 – 1440) |
| Timezone (POSIX TZ, for the clock only) | `SAST-2` |
| RGB LED shows mood | on |

## Controls

| Input | Action |
| --- | --- |
| Tap the screen / click BOOT | Refresh now (30 s cooldown) |
| Double-click BOOT | Cycle backlight brightness |
| Hold BOOT 3 s | Open / close the setup hotspot |
| Tap the **Almost there** screen | Open the setup hotspot |

The device also refreshes on every interval, right after you save settings, and shortly after the monthly reset. If a refresh fails it keeps showing the last good value (it's also kept across reboots). Network errors are retried after 2 minutes.

## Serial console (115200 baud)

| Command | Purpose |
| --- | --- |
| `status` | WiFi, settings (token masked), last result, heap |
| `refresh` | Fetch now, ignoring the cooldown |
| `set user <name>` / `set token <pat>` / `set limit <n>` / `set refresh <min>` / `set tz <TZ>` / `set led on\|off` | Change settings over USB (`set token -` forgets the token) |
| `demo` | Cycle through all moods every 4 s (toggle) |
| `pct <n>` / `pct off` | Force a percentage to preview a mood |
| `portal` | Open / close the setup hotspot |
| `screenshot` | Dump the screen as raw RGB565 (byte-swapped) rows |
| `httptest` | Request the settings page from the device itself |
| `wipe` | Erase WiFi and all settings, then restart |
| `reboot` | Restart |

## Troubleshooting

| Status bar | Meaning |
| --- | --- |
| `Token invalid (401)` | Token is wrong, expired or revoked |
| `Token needs Plan:read` | Token lacks the *Plan: Read-only* account permission |
| `User/billing not found` | Username typo, or Copilot is billed through an organization |
| `Can't reach GitHub` | DNS/TLS/network failure; retried after 2 minutes |
| `Data is stale` | No successful refresh for over two intervals |

If `http://<device-ip>/param` times out but `httptest` returns `200 OK`, your WiFi isolates clients (typical for guest networks). Use the setup hotspot instead.

## Security notes

- The token is stored unencrypted in the ESP32's NVS flash. Use a token limited to *Plan: Read-only*.
- The settings page on your LAN has no login, but it never shows the token. The WiFi menu and firmware updates are disabled on the LAN; WiFi changes go through the hotspot instead.
- HTTPS is verified against bundled root CAs ([src/app/github_ca.h](src/app/github_ca.h)). If GitHub moves to a CA that isn't in the bundle, add that root there.

## Code layout

| Area | Files |
| --- | --- |
| App flow, scheduler, inputs, serial console | [src/main.cpp](src/main.cpp) |
| Settings + cached reading (NVS) | [src/app/config.cpp](src/app/config.cpp) |
| GitHub API client and JSON parsing | [src/app/github_usage.cpp](src/app/github_usage.cpp), [src/app/github_usage_parse.h](src/app/github_usage_parse.h) |
| Moods, pace, projection, formatting (pure C++) | [src/app/usage_math.cpp](src/app/usage_math.cpp) |
| WiFiManager hotspot + LAN settings page | [src/app/wifi_setup.cpp](src/app/wifi_setup.cpp) |
| Pixel pilot mascot | [src/ui/copilot_mascot.cpp](src/ui/copilot_mascot.cpp) |
| Dashboard, setup screens, QR codes | [src/ui/dashboard.cpp](src/ui/dashboard.cpp), [src/ui/setup_screens.cpp](src/ui/setup_screens.cpp), [src/ui/qr.cpp](src/ui/qr.cpp) |

### Hardware drivers

| Module | Files | Notes |
| --- | --- | --- |
| Pin map | [src/board.h](src/board.h) | Button, RGB LED, LDR, speaker, touch, SD |
| Display + backlight | [src/drivers/display.cpp](src/drivers/display.cpp) | ILI9341 via TFT_eSPI on HSPI, PWM backlight |
| Touch | [src/drivers/touch.cpp](src/drivers/touch.cpp) | XPT2046 over software SPI, rotation-aware |
| RGB LED | [src/drivers/rgb_led.cpp](src/drivers/rgb_led.cpp) | Active low |
| microSD | [src/drivers/sdcard.cpp](src/drivers/sdcard.cpp) | VSPI, exposes the global `SD` object (unused) |

If touches are offset, adjust the `CAL_*` constants in [src/drivers/touch.cpp](src/drivers/touch.cpp) using values from `touchReadRaw()`.

## License

MIT, see [LICENSE](LICENSE). Based on [NerdMiner_v2](https://github.com/BitMaker-hub/NerdMiner_v2) by Bitmaker.

This is an independent project, not affiliated with or endorsed by GitHub. GitHub and Copilot are trademarks of GitHub, Inc.
