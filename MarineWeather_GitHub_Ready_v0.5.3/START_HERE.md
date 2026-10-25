# START HERE — Marine Weather Display v0.5.3

This ZIP is ready to upload to a **public** GitHub repository. It includes the complete Arduino sketch, GitHub Actions build workflow, installer site, and instructions. **A live site and compiled firmware do not exist until the GitHub Action completes successfully.**

1. Go to https://github.com/new and create a public repository named `marine-weather-display`. Do not add a README/license there yet; those files are in this ZIP.
2. Install GitHub Desktop from https://desktop.github.com and sign in.
3. Choose **File → Clone repository**, select your empty repository, and clone it on your PC.
4. Extract this ZIP and copy ALL files and folders from the extracted directory into the cloned repo folder. **Include `.github/workflows/publish.yml`**. Do not copy the ZIP itself into the repo.
5. In GitHub Desktop, enter a summary such as `Initial Marine Weather Display v0.5.3`, **Commit to main**, and **Push origin**.
6. On github.com, open your repository **Settings → Pages** and under **Build and deployment → Source**, choose **GitHub Actions**.
7. Open the repo **Actions** tab. If the Build firmware and publish installer workflow has not run after the push, select it and press **Run workflow**. Confirm green success for firmware build and Pages deploy.
8. Your website URL should be `https://YOURUSERNAME.github.io/marine-weather-display/` (also shown under **Settings → Pages**).
9. In Chrome or Edge on a PC, connect a **spare/test ESP32** via a USB data cable and test the installer before putting the URL on MakerWorld. Flashing may erase existing code/settings.

Detailed setup and safety instructions: `PUBLISHING.md`, `README.md`.

**Privacy:** Do not put your home Wi-Fi name/password in the public GitHub repository. This firmware configures them on the ESP32 after installation.
