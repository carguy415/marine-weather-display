# Publish the website and firmware using GitHub

These steps turn the included files into a **working public USB installer** once the firmware compiles successfully. You can do this without hosting a server or paying for a domain.

## 1. Test the new Arduino sketch first

Test `firmware/MarineWeather/MarineWeather.ino` on your existing ESP32 using Arduino IDE. Save a backup of the original **working** sketch and enter settings from the temporary AP. Check the forecast, BOOT short/long press, and configurable backlight timeout, including the Always on option. If there is a compile error, fix it before publishing publicly.

## 2. Create a public GitHub repository

- Log in to https://github.com and create a **new public repository**, e.g. `marine-weather-display`.
- Use **GitHub Desktop** (https://desktop.github.com) if you want a simple way to import/upload *all* folders. Extract this ZIP and copy **all of its contents** to the repository root, **including `.github/workflows/publish.yml`** (a hidden `.github` folder).
- Commit and push these files to the **`main`** branch.
- Do **not** put a real home Wi-Fi password or secret in any tracked file. None is required by the new sketch.

## 3. Enable GitHub Pages

In the repository:

1. Open **Settings → Pages**.
2. Under **Build and deployment**, select **Source: GitHub Actions**.
3. Open **Actions** and select **Build firmware and publish installer**. If the workflow has not run, choose **Run workflow**.
4. Let it compile the ESP32 firmware and deploy the public site. Look for a successful green check. The Actions logs are where to diagnose compiler or deployment errors.

The workflow performs `arduino-cli` compilation, uses the core-generated **merged ESP32 image**, copies it to `site/firmware.bin`, and deploys `site` to HTTPS GitHub Pages. `site/manifest.json` points at that resulting file. You do **not** need to compile a firmware binary locally for the website if this workflow succeeds.

## 4. Get the shareable install link

Your finished installer should be at:

`https://YOUR-GITHUB-USERNAME.github.io/marine-weather-display/`

Change the username/repository name to match your actual repository; the real URL is shown in **Settings → Pages** after deployment.

Open the site on **desktop Chrome/Edge** with the ESP32 connected via USB and test it once more, including first-time setup. Test that the device displays the **same setup Wi-Fi password** on successive 4-second BOOT holds/restarts. A complete flash/NVS erase will generate a new password. ESP Web Tools requires HTTPS, a compatible browser, a classic ESP32, and a USB data cable.

## 5. Link it from MakerWorld

Add that exact HTTPS URL to your MakerWorld model listing. Copy/edit `MAKERWORLD_DESCRIPTION.md` as the assembly instructions.

## Troubleshooting

- **The Install button doesn't work:** Check the Pages deployment completed and `firmware.bin` exists in the published output. Opening `site/index.html` from disk is not sufficient; it must be served over HTTPS.
- **The build fails:** Open the workflow log, inspect the compile error, fix the source, commit, and push again. New builds automatically republish.
- **Wrong board detected:** This first release is for **classic ESP32 DevKit**, not ESP32-S3, ESP32-C3, etc.
- **No serial device:** Try a known **data USB cable**, install the ESP32 DevKit USB-to-serial driver if required, and close Arduino IDE's serial monitor.
- **Can't connect to home Wi-Fi:** ESP32 supports **2.4GHz** networks; check SSID/password and router mode. Failed Wi-Fi on startup opens the temporary local setup AP again.
- **No forecast for a zone:** Check the exact NOAA coastal text file exists and contains `.TODAY...` period headings. Unsupported directories/formats need code changes.

### Important status

These project files are ready to upload to a GitHub repository, but **no public website has been deployed and no verified compiled `.bin` is included in this download**. A successful compilation/deployment and a real-board test are required before users can actually use the public installer.
