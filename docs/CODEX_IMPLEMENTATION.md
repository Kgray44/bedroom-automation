# Codex implementation and deployment handoff · Doorlight 06

## Mission

Continue the source in `Kgray44/bedroom-automation`, targeting the new **standalone Doorlight ESP32-S3**. The user explicitly requires **NO code or firmware changes to `Kgray44/fingerprintdoorlock` or `Kgray44/Bedroom_Lights`**, and no second motion sensor.

The PC game watcher is `pc-gaming/Watch-DoorlightGames.ps1`; the strip transition implementation is `BridgeController/BedroomSnapshotEngine.h`. These are real first-party project components, not placeholders to replace with generic planning notes.

## Nonnegotiable acceptance criteria

1. Run `arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=min_spiffs BridgeController` using ESP32 Arduino core and `WebSockets` (Markus Sattler), `ArduinoJson`. Resolve compile failures in the **Doorlight repo only**. Create exact reproducible build instructions and report board/core/library versions. Confirm GPIO18 on S3 (GPIO23 is not usable on that target), plus OTA-capable partition settings (use `PartitionScheme=min_spiffs` for the baseline build, not `huge_app`; confirm physical flash-size setting matches the board).
2. For the strip, use **only existing** `/api/state`, `/set`, `/api/palettes/select`, and `/api/transitions/set`. Capture baseline, set native fade for alert, apply effect, read back, wait, configure restore fade, restore baseline and palette, wait for completion, restore original fade configuration, read back and compare. Preserve NVS recovery data and 3-attempt bounded retries. Never clobber a manual change. Test failure at each state-machine boundary, reboot and Wi-Fi reconnect.
3. Keep `GET/PUT /api/bedroom/fades` and the dashboard fade panel consistent. The only legal native durations are 0, 500, 1500, 3000, 5000 ms. For very short alerts do not claim that a five-second fade necessarily completes inside a two-second alert.
4. Windows watcher must detect exact configured executable names with PowerShell Get-Process. Provide `.json` game list excluding API keys, secure local DPAPI API key under Windows user account, debounced start/stop, heartbeat lease to `/api/gaming`. It must not stop Gaming Mode controlled manually by another user. Document Task Scheduler installation.
5. On ESP32, `POST /api/gaming` must respect `source=manual|pc-watcher`, reject source conflicts, accept bounded `leaseSeconds=30..600`, refresh lease on heartbeat, and stop/restore after lease expiry. Provide a test that a dead Windows watcher can't leave Gaming Mode active forever.
6. Keep Home Assistant Matter bulbs integration as a local API client only; actual Matter commissioning is done in Home Assistant. No cloud service required for everyday commands. Preserve saved scenes when temporary bulbs or gaming lighting finish.
7. Preserve local Night Mode (18:00–07:00, default 50%), Everyone/Roommates schedules (06:15/08:00), source-level security (API key and auth), firmware OTA and modern responsive dashboard. Check data sizes/memory on ESP32-S3.
8. Do not commit `Secrets.h`, Home Assistant long-lived access tokens, real Wi-Fi passwords, private fingerprint details, or Windows encrypted key. Protect the public repository and rotate any leaked secrets.
9. Test PS 5.1 watcher through Pester or Windows VM: `-Setup`, `-Once`, game start/exit, manual Game Mode conflict, 409 errors, Wi-Fi loss and ESP32 reboot. Build automation tests with mocked HTTP and deterministic clock. Document failures instead of falsely claiming hardware pass.
10. When ready: open a PR with evidence of toolchain build, static and native tests, and hardware test procedure. Do not merge if build failed or original-device writes were needed.

## Codex-ready request

> In repository `Kgray44/bedroom-automation`, complete the ESP32-S3 integration and verify it end-to-end. Focus on the native bedroom-strip fade-into-alert/fade-back restoration engine, the Windows Gaming Mode PowerShell watcher with encrypted API-key storage, and source-owned PC lease watchdog. Keep both existing fingerprint lock and bedroom-light firmware **completely unchanged**. Inspect the actual code before editing, fix all compilation errors and race conditions, add unit/integration tests and a reproducible `arduino-cli` build, validate dashboard API behavior, and open a PR containing test/build results. No fake successes, placeholder functions, or speculative hardware claims. The repository is public: never commit secrets. Also verify that all prior Doorlight 05 features remain working, including schedules, OTA, Night Mode, Smart Bulbs and Shortcuts.
