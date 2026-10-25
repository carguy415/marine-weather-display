# DIY NOAA Marine Weather Display — ESP32 + 2-inch TFT

Build a compact marine weather screen that shows NOAA coastal waters forecasts on a 2-inch TFT display. The project cycles through the first four forecast periods, displaying wind, seas, wave details, and NOAA marine advisories when included in the source forecast.

**Features:** Wi-Fi marine forecasts updated every 30 minutes; press BOOT to change forecast periods; adjustable screen/backlight timeout (15 seconds to 10 minutes, or always on; default 30 seconds); press BOOT to wake; hold BOOT for 4 seconds to reconfigure Wi-Fi and marine forecast zone.

**Parts:** classic ESP32-WROOM-32 DevKit, GMT020-02-8P 2-inch ST7789 240×320 SPI TFT (or independently tested electrical equivalent), jumper wiring, USB power, and the printed enclosure.

**Wiring:** TFT GND→ESP32 GND; VCC→3V3; SCL→D18; SDA→D23; RST→D22; DC→D19; CS→D5; BL→D27. BL uses direct GPIO control only because the original screen's backlight control connection measured 2.4 mA at 3.3V. Do not assume all ST7789 modules have the same backlight design.

**Install the software:** [PASTE YOUR REAL GITHUB PAGES INSTALL LINK HERE]

1. Use a computer with Chrome or Edge and connect your ESP32 by USB.
2. Click **Connect & Install** on the installer site.
3. Find your marine zone code on NOAA's [Coastal Marine Forecast Zone Map](https://www.weather.gov/marine/usamz) while you still have internet access (example: ANZ331).
4. The display will show its own `MARINEWX-...` setup Wi-Fi and a random 12-character password. Each unit generates a different password, which stays the same after each reboot; your phone can remember it. Join it from your phone.
5. Browse to **http://192.168.4.1**, enter your home Wi-Fi name/password, enter your marine forecast zone code, and select a backlight timeout.
6. Save and enjoy the forecast; you won't need to edit Arduino code. To change the timeout later, hold BOOT for 4 seconds while running and reopen the local setup page.

**Compatibility:** This version is intended for classic ESP32 DevKit hardware and NOAA **coastal** zones using compatible NWS text files (ANZ, AMZ, GMZ, PZZ). Some zone codes and forecast formats will not work. Test the setup on your own hardware.

**Disclaimer:** Hobby display only; not a certified marine weather instrument. Always consult official NOAA/NWS forecasts and alerts before boating. Not affiliated with NOAA/NWS, Espressif, or MakerWorld.


The setup page remembers up to **eight recently used marine forecast zone codes** on each ESP32, so you can quickly choose a previous area from the original zone field without typing it again. **Press and hold** a saved zone in the list for about one second to remove it from history.
