# Marine Weather Display — public maker project (prototype v0.5.3)

A DIY NOAA marine weather display built around a **classic ESP32-WROOM-32 DevKit** and a **2-inch ST7789 240×320 8-pin SPI TFT**. This package contains the entire Arduino sketch, a public ESP Web Tools installer website, and a GitHub Actions workflow that builds the USB firmware and publishes the site on GitHub Pages.

**Development status:** v0.5.0 and its saved-zone history were confirmed working on the original ESP32. The new v0.5.3 splash screen needs to be tested on the ESP32 before publishing. The website Install button will not work until GitHub Actions successfully generates and deploys `firmware.bin`.

## Files

- `firmware/MarineWeather/MarineWeather.ino` — universal Arduino sketch; no personal Wi-Fi credentials embedded.
- `site/index.html` — public installation and setup instruction page.
- `site/manifest.json` — ESP Web Tools manifest for a **classic ESP32 only**.
- `.github/workflows/publish.yml` — compile merged firmware and deploy everything to GitHub Pages on pushes to `main`.
- `PUBLISHING.md` — step-by-step GitHub setup.
- `MAKERWORLD_DESCRIPTION.md` — copyable MakerWorld listing description.

## Exact wiring for the original tested hardware

| Screen pin | ESP32 DevKit pin |
|---|---|
| GND | GND |
| VCC | 3V3 |
| SCL | GPIO18 / D18 |
| SDA | GPIO23 / D23 |
| RST | GPIO22 / D22 |
| DC | GPIO19 / D19 |
| CS | GPIO5 / D5 |
| BL | GPIO27 / D27 |

The original GMT020-02-8P display's BL input drew **2.4 mA** from 3.3V, measured on the prototype. Other ST7789 modules may have different backlight circuits, so don't assume GPIO27 can safely power all backlights. Never connect BL to 3V3 and GPIO27 at the same time. This wiring is specific to the original **classic ESP32-WROOM-32 DevKit**; ESP32-S3/C3 boards can have different pin mappings.

## What it does

- Download NOAA coastal forecast text every **30 minutes**; retries failed updates every minute.
- Keep the last good forecast in RAM on temporary connection failures.
- Display up to **four lines of WIND**, **three lines of SEAS**, and a permanent **WAVE DETAIL** row at the bottom. When NOAA does not provide wave details for a period, show **Not provided** rather than hiding the row.
- Display a marine advisory when the source text includes a recognized advisory.
- **Short BOOT press when awake:** show next forecast period.
- **Short BOOT press while asleep:** wake display, without changing the page.
- Turn LCD image and GPIO27-controlled backlight off after a **user-selectable period** (15 seconds, 30 seconds, 1, 2, 5, or 10 minutes; or always on). Default is 30 seconds.
- **Hold BOOT for 4 seconds while running:** reopen the local Wi-Fi/zone setup page.
- Remember Wi-Fi, zone, and backlight timeout across power cycles using the ESP32's non-volatile preferences.
- Show up to **8 previously selected forecast zones** in the original editable zone field’s dropdown. The most recently used appears first; duplicates are removed, and the history survives restarts.

## First-time setup (no Arduino IDE for end users)

1. Use a **desktop** Chromium browser (Chrome or Edge) and a **data-capable USB cable**.
2. Connect the classic ESP32 DevKit, visit your deployed installer site, and click **Connect & Install**.
3. After first boot, the display shows a Wi-Fi access point named `MARINEWX-....`, a **random 12-character password generated once per ESP32 and saved in nonvolatile memory**, and `192.168.4.1`.
4. Before joining the ESP32 Wi-Fi, find your coastal forecast zone using NOAA's [Coastal Marine Forecast Zone Map](https://www.weather.gov/marine/usamz) and note its six-character code (e.g. `ANZ331`).
5. On your phone, join the ESP32 access point using the password printed on the TFT. If your phone reports **no internet**, choose *stay connected*.
6. Visit **http://192.168.4.1** in a browser (if the captive page does not open automatically).
7. Enter your **home 2.4GHz Wi-Fi** network name, its password, your NOAA marine forecast zone, e.g. `ANZ331` (or select a previously used zone when available), and your preferred **backlight timeout**.
8. Save. The ESP32 restarts, connects to home Wi-Fi, and downloads NOAA's forecast.

**No Wi-Fi password is submitted to the public installer website.** The credentials are sent to the device over its temporary WPA2-protected local access point and stored in its flash memory. The on-device setup form uses HTTP on that local network, not HTTPS. It is intended for household setup, not use in an untrusted location; someone who has the setup network password can also access that local page.

## Change settings later

While ESP32 is **running**, hold BOOT for **4 seconds**. It will show the same setup network and password assigned to this ESP32 on its first setup. Your phone can remember it. Rejoin it from your phone and change the zone, Wi-Fi information, or backlight timeout. A short press during normal operation changes forecast screens. **Do not hold BOOT during reset or power-up** — GPIO0 is also an ESP32 flash-mode strap pin.

## Supported zone codes

The prototype builds NOAA text URLs from six-character zone codes in the coastal directories:

- `ANZ###` (Atlantic north, e.g. ANZ331, ANZ335, ANZ232)
- `AMZ###` (Atlantic south)
- `GMZ###` (Gulf)
- `PZZ###` (Pacific)

Example ANZ331: `https://tgftp.nws.noaa.gov/data/forecasts/marine/coastal/an/anz331.txt`

**Not every valid-looking zone code is an individual marine forecast.** Some are headings or group products, and NOAA text formatting can vary by office. If no forecast appears, verify that the exact zone file exists, contains period lines like `.TODAY...`, and includes that zone's code. Lake, offshore, Alaska, and Hawaii products use other directories and are **not supported in v0.5.3**.

## Optional local Arduino IDE test

1. Install Espressif **ESP32** board package and choose **ESP32 Dev Module**.
2. Install **Adafruit GFX Library** and **Adafruit ST7735 and ST7789 Library**.
3. Open `firmware/MarineWeather/MarineWeather.ino` and upload to your ESP32 (USB).
4. Open Serial Monitor at **115200 baud** to debug Wi-Fi or HTTP response failures.
5. Follow the same temporary AP setup instructions above.

## Security & safety limitations

- The firmware currently uses `client.setInsecure()` for downloading NOAA's HTTPS text product: transport is encrypted, but the server certificate **is not authenticated**. This is suitable for testing, but should be improved before reliance in production.
- There is no live weather sensor; values are NOAA **forecasts**, not measured wind at your location.
- The source parser prioritizes the first four forecast periods and simplifies longer sentences; some text will be truncated on this small screen.
- The demo doesn't guarantee marine hazards are complete or current. **Use official NOAA/NWS products and other approved sources when making boating safety decisions.**
- A first-time install or full flash erase may overwrite user code and saved preferences.
- Test the exact display module and GPIO27 BL current before advertising compatibility with other screens.

## Sources

- Official NOAA text feed: https://tgftp.nws.noaa.gov/data/forecasts/marine/coastal/an/anz331.txt
- NOAA coastal forecast zone map: https://www.weather.gov/marine/usamz
- ESP Web Tools: https://esphome.github.io/esp-web-tools/

## License

MIT; see `LICENSE`.

## Backlight timeout settings

Hold **BOOT for 4 seconds while running** and open the local setup page at `http://192.168.4.1`. Choose **Backlight timeout** from the dropdown (15 seconds, 30 seconds, 1 minute, 2 minutes, 5 minutes, 10 minutes, or always on), then save and restart. Your choice is saved in ESP32 Preferences and survives power cycles. The default remains 30 seconds if no option has been stored. Selecting **Always on** disables the sleep timer in normal forecast mode; it does not disable automatic NOAA updates. **Setup mode keeps the backlight illuminated** to show the temporary network details.

## Per-device setup Wi-Fi passwords (v0.4)

On the very first setup, each ESP32 generates its own random 12-character
WPA2 setup password and saves it in the device's nonvolatile Preferences
(NVS) storage under `marinewx/ap_pass`. On later visits to setup mode
(hold BOOT for 4 seconds), it reuses the saved password. The TFT shows the
network name and password every time, so you can reconnect your phone
without entering a new password. There is no default shared password.

**Normal restart/power loss and normal Arduino firmware updates preserve
the password. A full flash/NVS erase (including an installer configured to
erase all flash) resets it**, and the board generates a new one at next setup.
The setup password is distinct from the home Wi-Fi password and is never
embedded in the public firmware or uploaded to the project website.



## New in v0.5.3: geographically nearby marine zones

The one editable zone field still holds the current code. Open its dropdown to
see saved zones and **nearby suggestions for that code**. Suggestions change
when a different zone is entered or selected, without saving it first.
When home Wi-Fi is connected, ESP32 can download official NWS coastal marine
zone **centroids** (not live marine forecasts) from the public NOAA GIS service,
rank areas within 250 km of the selected zone, and save up to six suggestions
in nonvolatile Preferences. Later visits use the cache even offline.

A small locally bundled verified list covers Long Island Sound and nearby
southern New England zones when online GIS access is unavailable. For other
regions the user can enter a zone from the [NOAA coastal marine zone map](https://www.weather.gov/marine/usamz)
and will get local recommendations after the first successful online lookup.
Centroid distance is **approximate geographic proximity**, not a promise that
zone polygons share a border, or that every returned forecast format is supported.

A first-time ESP32 without a configured Wi-Fi connection can't reach the NOAA
GIS service, so nearby suggestions outside the built-in local examples won't
appear until home Wi-Fi has been configured. The rest of setup works offline.
The app's forecast data continues to come from NOAA's marine text feed.
