# Windows Gaming Mode companion

This is the **only** code that runs on your PC. The ESP32 keeps controlling the bulbs through Home Assistant. The companion never touches the original fingerprint lock or existing LED strip firmware.

## Install

1. Have `Doorlight/BridgeController/BridgeController.ino` installed with Home Assistant and the two Matter bulbs configured, and verify `/api/routines` works from your PC.
2. Copy `games.example.json` to `games.json` in this directory; replace `YOUR-GAME-EXECUTABLE.exe` with exact executable names visible in Task Manager > Details. Do **not** put your API key in the JSON file.
3. Open PowerShell **as your normal Windows user** (not elevated as another account), run `Set-ExecutionPolicy -Scope Process Bypass`, then run `./Watch-DoorlightGames.ps1 -Setup` and paste the private Doorlight API key. The key is encrypted using Windows DPAPI for the current user in `%LOCALAPPDATA%\Doorlight\api-key.dpapi`.
4. Run `./Watch-DoorlightGames.ps1 -Once` to check game detection without controlling lights.
5. Run `./Watch-DoorlightGames.ps1` to monitor. Adjust process names and debounce periods in your local `games.json` (excluded from Git). Run under the *same user account* that saved the DPAPI key.
6. To start automatically, use Windows Task Scheduler: **At logon** of your account, action `powershell.exe`, arguments `-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "C:\path\to\Watch-DoorlightGames.ps1"`, and "Start in" the `pc-gaming` folder. Only run while logged on; do not store the secret in task arguments.

## Behavior and safeguards

- Exact `.exe` name matching via `Get-Process`, case-insensitive. No injected code, telemetry, game memory access, window scraping, or anti-cheat hacks.
- Waits 10 seconds before starting, 25 seconds after all configured games exit before stopping. This avoids brief launcher transitions.
- Uses `POST /api/gaming` with `{"active":true,"source":"pc-watcher","leaseSeconds":90}` and refreshes it every 20 seconds. Loss of process/PC/network lets the ESP32 automatically expire and restore the session.
- The watcher will **not** take control of Gaming Mode if someone activated it manually. On exit it only stops the `pc-watcher` source, never a manual session.
- Other lighting automation can still run if explicitly configured, but Gaming Mode suppresses the ordinary motion alert patterns when the dashboard toggle is enabled.
- The smart-bulb restore path is the Home Assistant scene snapshot and restore. Manual bulb edits during Gaming Mode are protected best-effort by existing state/context checks.
- If you're using gaming with the existing LED strip too, author a separate temporary strip shortcut. This companion does not alter its firmware or automatically force strip effects.

## Security and troubleshooting

The ESP32's HTTP API is on trusted LAN only. Do not forward its port to the Internet. Since the original door firmware's control WebSocket lacks strong authentication, isolate it on trusted Wi-Fi as well.

Use `-Verbose` to view lease renewal messages. If commands fail, check Wi-Fi, Home Assistant, `/api/routines`, API-key setup and device addresses. To revoke the PC key, change the ESP32 API key and re-run `-Setup`.
